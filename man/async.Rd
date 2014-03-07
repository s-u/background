\name{async}
\alias{async}
\alias{async.add}
\alias{async.rm}
\title{
  Asynchronous input callbacks
}
\description{
  \code{async.add} registers a new asynchronous callback

  \code{async.rm} removes an asynchronous handler
}
\usage{
async.add(fd, callback = function(h, data) async.rm(h), data = NULL)
async.rm(handler)
}
\arguments{
  \item{fd}{file descriptor (of any kind including sockets or pipes)
    which will trigger a callback}
  \item{callback}{callback function}
  \item{data}{any user data that will be passed as the second argument
    to the callback (after the handler)}
  \item{handler}{handler as returned by \code{async.fd} or passed as the
    first argument to the callback function}
}
\details{
  Asynchronous callbacks trigger when R is idle and read activity is
  signalled on the file descriptor. The activity can be incoming
  connection, data or similar.

  It is important that the callback resolves the reason for the trigger
  or removes the handler. Failing to do so will result in infinite
  calling of the callback handler.
}
\value{
  \code{async.add} returns the (opaque) handler object

  \code{async.rm} return \code{TRUE}
}
%\references{
%}
\author{
  Simon Urbanek
}
%\seealso{
%}
\examples{
\dontrun{
   future = function(expr) {
      p = parallel::mcparallel(expr)
      async.add(p$fd[1], function(h, p) {
         async.rm(h)
         print(parallel::mccollect(p)[[1]])
      }, p)
      invisible(p)
   }

   future({ Sys.sleep(5); "done!" })
   ## in 5 seconds you'll see an output
}
}
\keyword{interface}
