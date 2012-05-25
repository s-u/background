/*
 *  async.c - ansynchronoud callback into R based on FD activity
 *  Copyright (C) 2012 Simon Urbanek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, a copy is available at
 *  http://www.r-project.org/Licenses/
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define USE_RINTERNALS 1
#include <Rinternals.h>

#define BackgroundActivity 10

#ifndef WIN32
#include <R_ext/eventloop.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

static int in_process;

typedef struct bg_conn {
    struct bg_conn *next, *prev;
    int fd;
    SEXP callback;
    SEXP user;
    SEXP self;
#ifdef WIN32
    HANDLE thread;       /* worker thread */
#else
    InputHandler *ih;    /* worker input handler */
#endif
} bg_conn_t;

static bg_conn_t *handlers;

#ifdef WIN32
#define WM_BACKGROUND_CALLBACK ( WM_USER + 1 )
static HWND message_window;
static LRESULT CALLBACK
RhttpdWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#ifndef HWND_MESSAGE
#define HWND_MESSAGE ((HWND)-3) /* NOTE: this is supported by W2k/XP and up only! */
#endif
#endif

static int needs_init = 1;

static void first_init()
{
#ifdef WIN32
    /* create a dummy message-only window for synchronization with the
     * main event loop */
    HINSTANCE instance = GetModuleHandle(NULL);
    LPCTSTR class = "background";
    WNDCLASS wndclass = { 0, BackgroundWindowProc, 0, 0, instance, NULL, 0, 0,
			  NULL, class };
    RegisterClass(&wndclass);
    message_window = CreateWindow(class, "background", 0, 1, 1, 1, 1,
				  HWND_MESSAGE, NULL, instance, NULL);
#endif
    needs_init = 0;
}

static void finalize_handler(bg_conn_t *c)
{
#ifndef WIN32
    if (c->ih) {
	removeInputHandler(&R_InputHandlers, c->ih);
	c->ih = NULL;
    }
#endif
    if (c->prev) {
	c->prev->next = c->next;
	c->next->prev = c->prev;
    } else if (c->next)
	c->next->prev = 0;
    if (handlers == c)
	handlers = c->next;
    if (c->callback != R_NilValue)
	R_ReleaseObject(c->callback);
    if (c->user != R_NilValue)
	R_ReleaseObject(c->user);
    R_ReleaseObject(c->self);
    /* FIXME: free? so far we leave that for the caller ... */
}

#ifdef WIN32
/* on Windows we have to guarantee that run_callback is performed
   on the main thread, so we have to dispatch it through a message */
static void run_callback_main_thread(bg_conn_t *c);

static void run_callback(bg_conn_t *c)
{
    /* SendMessage is synchronous, so it will wait until the message
       is processed */
    SendMessage(message_window, WM_BACKGROUND_CALLBACK, 0, (LPARAM) c);
}
#define run_callback run_callback_main_thread
#endif

/* process a request by calling the callback in R */
static void run_callback_(void *ptr)
{
    bg_conn_t *c = (bg_conn_t*) ptr;
    SEXP what = PROTECT(lang3(c->callback, c->self, c->user));
    eval(what, R_GlobalEnv);
    UNPROTECT(1);
}

/* wrap the actual call with ToplevelExec since we need to have a guaranteed
   return so we can track the presence of a worker code inside R to prevent
   re-entrance from other clients */
static void run_callback(bg_conn_t *c)
{
    if (in_process) return;
    in_process = 1;
    R_ToplevelExec(run_callback_, c);
    in_process = 0;
}

#ifdef WIN32
#undef run_callback
#endif

static void callback_input_handler(void *data);

#ifdef WIN32
/* Windows implementation uses threads to watch the FD and the main event
   loop to synchronize with R through a message-only window which is created
   on the R thread */
static LRESULT CALLBACK BackgroundWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (hwnd == message_window && uMsg == WM_BACKGROUND_CALLBACK_CALLBACK) {
	bg_conn_t *c = (bg_conn_t*) lParam;
	run_callback_main_thread(c);
	return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

/* worker thread - processes one client connection socket */
static DWORD WINAPI BackgroundThreadProc(LPVOID lpParameter) {
    bg_conn_t *c = (bg_conn_t*) lpParameter;
    if (!c) return 0;

    /* FIXME: can we use select() on Windows? Does it work with FDs and sockets? or something else? */

    return 0;
}
#endif

/* this is really superfluous - we could jsut cast run_callback accordingly .. */
static void callback_input_handler(void *data)
{
    run_callback((bg_conn_t*) data);
}

SEXP bg_add(SEXP s_fd, SEXP callback, SEXP user)
{
    int fd = Rf_asInteger(s_fd);
    bg_conn_t *c;

    if (needs_init) /* initialization - creates the virtual window on Windows */
	first_init();

    c = (bg_conn_t*) calloc(1, sizeof(bg_conn_t));
    if (!c)
	Rf_error("out of memory");

    if (handlers) {
	c->next = handlers;
	if (handlers) handlers->prev = c;
    }
    handlers = c;
    
    c->fd = fd;
    c->callback = callback;
    R_PreserveObject(callback);
    c->user = user;
    if (user != R_NilValue)
	R_PreserveObject(user);
    R_PreserveObject(c->self = R_MakeExternalPtr(c, R_NilValue, R_NilValue));
    Rf_setAttrib(c->self, Rf_install("class"), mkString("BackgroundHandler"));
#ifndef WIN32
    c->ih = addInputHandler(R_InputHandlers, fd, &callback_input_handler, BackgroundActivity);
    if (c->ih) c->ih->userData = c;
#else
    c->thread = CreateThread(NULL, 0, BackgroundThreadProc, (LPVOID) c, 0, 0);
#endif
    return c->self;
}

/* remove a handler */
SEXP bg_rm(SEXP h) {
    bg_conn_t *c;
    if (TYPEOF(h) != EXTPTRSXP || !inherits(h, "BackgroundHandler"))
	Rf_error("invalid handler");
    c = (bg_conn_t*) EXTPTR_PTR(h);
    finalize_handler(c);
    free(c);
    return ScalarLogical(TRUE);
}

#ifndef WIN32

/** -- test pipe -- forks and sends a byte to trigger a callback */

SEXP fpipe() {
    int fd[2];
    pid_t pid;
    pipe(fd);
    if ((pid = fork()) == 0) {
	close(fd[0]);
	printf("child, sleeping\n");
	sleep(4);
	printf("child, writing\n");
	write(fd[1], "X", 1);
	close(fd[1]);
	printf("child, done\n");
	exit(0);
    }
    close(fd[1]);
    return ScalarInteger(fd[0]);
}

/* read one byte from a FD; returns -1 on close/error */
SEXP frd(SEXP s_fd) {
    unsigned char b;
    int fd = Rf_asInteger(s_fd);
    if (read(fd, &b, 1) < 1) {
	close(fd);
	return ScalarInteger(-1);
    }
    return ScalarInteger((int)b);
}

#endif

#if 0 /* just a reminder how to stop the thread if needed */
#ifdef WIN32
    if (c->thread) {
	DWORD ts = 0;
	if (GetExitCodeThread(c->thread, &ts) && ts == STILL_ACTIVE)
	    TerminateThread(c->thread, 0);
    }
#endif
#endif
