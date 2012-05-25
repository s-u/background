async.add <- function(fd, callback=function(h, data) async.rm(h), data=NULL) .Call("bg_add", fd, callback, data, PACKAGE="background")

async.rm <- function(handler) .Call("bg_rm", handler, PACKAGE="background")
