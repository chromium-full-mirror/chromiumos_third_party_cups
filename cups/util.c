/*
 * Printing utilities for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2018 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include "cups-private.h"
#include "debug-internal.h"
#include "error-codes.h"
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <regex.h>
#include <sys/stat.h>
#if defined(_WIN32) || defined(__EMX__)
#  include <io.h>
#else
#  include <unistd.h>
#endif /* _WIN32 || __EMX__ */


/*
 * 'cupsCancelJob()' - Cancel a print job on the default server.
 *
 * Pass @code CUPS_JOBID_ALL@ to cancel all jobs or @code CUPS_JOBID_CURRENT@
 * to cancel the current job on the named destination.
 *
 * Use the @link cupsLastError@ and @link cupsLastErrorString@ functions to get
 * the cause of any failure.
 *
 * @exclude all@
 */

int					/* O - 1 on success, 0 on failure */
cupsCancelJob(const char *name,		/* I - Name of printer or class */
              int        job_id)	/* I - Job ID, @code CUPS_JOBID_CURRENT@ for the current job, or @code CUPS_JOBID_ALL@ for all jobs */
{
  return (cupsCancelJob2(CUPS_HTTP_DEFAULT, name, job_id, 0)
              < IPP_STATUS_REDIRECTION_OTHER_SITE);
}


/*
 * 'cupsCancelJob2()' - Cancel or purge a print job.
 *
 * Canceled jobs remain in the job history while purged jobs are removed
 * from the job history.
 *
 * Pass @code CUPS_JOBID_ALL@ to cancel all jobs or @code CUPS_JOBID_CURRENT@
 * to cancel the current job on the named destination.
 *
 * Use the @link cupsLastError@ and @link cupsLastErrorString@ functions to get
 * the cause of any failure.
 *
 * @since CUPS 1.4/macOS 10.6@ @exclude all@
 */

ipp_status_t				/* O - IPP status */
cupsCancelJob2(http_t     *http,	/* I - Connection to server or @code CUPS_HTTP_DEFAULT@ */
               const char *name,	/* I - Name of printer or class */
               int        job_id,	/* I - Job ID, @code CUPS_JOBID_CURRENT@ for the current job, or @code CUPS_JOBID_ALL@ for all jobs */
	       int        purge)	/* I - 1 to purge, 0 to cancel */
{
  char		uri[HTTP_MAX_URI];	/* Job/printer URI */
  ipp_t		*request;		/* IPP request */


 /*
  * Range check input...
  */

  if (job_id < -1 || (!name && job_id == 0))
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

 /*
  * Connect to the default server as needed...
  */

  if (!http)
    if ((http = _cupsConnect()) == NULL)
      return (IPP_STATUS_ERROR_SERVICE_UNAVAILABLE);

 /*
  * Build an IPP_CANCEL_JOB or IPP_PURGE_JOBS request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    job-uri or printer-uri + job-id
  *    requesting-user-name
  *    [purge-job] or [purge-jobs]
  */

  request = ippNewRequest(job_id < 0 ? IPP_OP_PURGE_JOBS : IPP_OP_CANCEL_JOB);

  if (name)
  {
    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                     "localhost", ippPort(), "/printers/%s", name);

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
                 uri);
    ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id",
                  job_id);
  }
  else if (job_id > 0)
  {
    snprintf(uri, sizeof(uri), "ipp://localhost/jobs/%d", job_id);

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri", NULL, uri);
  }

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());

  if (purge && job_id >= 0)
    ippAddBoolean(request, IPP_TAG_OPERATION, "purge-job", 1);
  else if (!purge && job_id < 0)
    ippAddBoolean(request, IPP_TAG_OPERATION, "purge-jobs", 0);

 /*
  * Do the request...
  */

  ippDelete(cupsDoRequest(http, request, "/jobs/"));

  return (cupsLastError());
}


/*
 * 'cupsCreateJob()' - Create an empty job for streaming.
 *
 * Use this function when you want to stream print data using the
 * @link cupsStartDocument@, @link cupsWriteRequestData@, and
 * @link cupsFinishDocument@ functions.  If you have one or more files to
 * print, use the @link cupsPrintFile2@ or @link cupsPrintFiles2@ function
 * instead.
 *
 * @since CUPS 1.4/macOS 10.6@ @exclude all@
 */

int					/* O - Job ID or 0 on error */
cupsCreateJob(
    http_t        *http,		/* I - Connection to server or @code CUPS_HTTP_DEFAULT@ */
    const char    *name,		/* I - Destination name */
    const char    *title,		/* I - Title of job */
    int           num_options,		/* I - Number of options */
    cups_option_t *options)		/* I - Options */
{
  int		job_id = 0;		/* job-id value */
  ipp_status_t  status;                 /* Create-Job status */
  cups_dest_t	*dest;			/* Destination */
  cups_dinfo_t  *info;                  /* Destination information */


  DEBUG_printf(("cupsCreateJob(http=%p, name=\"%s\", title=\"%s\", num_options=%d, options=%p)", (void *)http, name, title, num_options, (void *)options));

 /*
  * Range check input...
  */

  if (!name)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);
    return (0);
  }

 /*
  * Lookup the destination...
  */

  if ((dest = cupsGetNamedDest(http, name, NULL)) == NULL)
  {
    DEBUG_puts("1cupsCreateJob: Destination not found.");
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(ENOENT), 0);
    return (0);
  }

 /*
  * Query dest information and create the job...
  */

  DEBUG_puts("1cupsCreateJob: Querying destination info.");
  if ((info = cupsCopyDestInfo(http, dest)) == NULL)
  {
    DEBUG_puts("1cupsCreateJob: Query failed.");
    cupsFreeDests(1, dest);
    return (0);
  }

  status = cupsCreateDestJob(http, dest, info, &job_id, title, num_options, options);
  DEBUG_printf(("1cupsCreateJob: cupsCreateDestJob returned %04x (%s)", status, ippErrorString(status)));

  cupsFreeDestInfo(info);
  cupsFreeDests(1, dest);

 /*
  * Return the job...
  */

  if (status >= IPP_STATUS_REDIRECTION_OTHER_SITE)
    return (0);
  else
    return (job_id);
}


/*
 * 'cupsFinishDocument()' - Finish sending a document.
 *
 * The document must have been started using @link cupsStartDocument@.
 *
 * @since CUPS 1.4/macOS 10.6@ @exclude all@
 */

ipp_status_t				/* O - Status of document submission */
cupsFinishDocument(http_t     *http,	/* I - Connection to server or @code CUPS_HTTP_DEFAULT@ */
                   const char *name)	/* I - Destination name */
{
  char	resource[1024];			/* Printer resource */


  snprintf(resource, sizeof(resource), "/printers/%s", name);

  ippDelete(cupsGetResponse(http, resource));

  return (cupsLastError());
}


/*
 * 'cupsFreeJobs()' - Free memory used by job data.
 */

void
cupsFreeJobs(int        num_jobs,	/* I - Number of jobs */
             cups_job_t *jobs)		/* I - Jobs */
{
  int		i;			/* Looping var */
  cups_job_t	*job;			/* Current job */


  if (num_jobs <= 0 || !jobs)
    return;

  for (i = num_jobs, job = jobs; i > 0; i --, job ++)
  {
    _cupsStrFree(job->dest);
    _cupsStrFree(job->user);
    _cupsStrFree(job->format);
    _cupsStrFree(job->title);
  }

  free(jobs);
}


/*
 * 'cupsGetClasses()' - Get a list of printer classes from the default server.
 *
 * This function is deprecated and no longer returns a list of printer
 * classes - use @link cupsGetDests@ instead.
 *
 * @deprecated@ @exclude all@
 */

int					/* O - Number of classes */
cupsGetClasses(char ***classes)		/* O - Classes */
{
  if (classes)
    *classes = NULL;

  return (0);
}


/*
 * 'cupsGetDefault()' - Get the default printer or class for the default server.
 *
 * This function returns the default printer or class as defined by
 * the LPDEST or PRINTER environment variables. If these environment
 * variables are not set, the server default destination is returned.
 * Applications should use the @link cupsGetDests@ and @link cupsGetDest@
 * functions to get the user-defined default printer, as this function does
 * not support the lpoptions-defined default printer.
 *
 * @exclude all@
 */

const char *				/* O - Default printer or @code NULL@ */
cupsGetDefault(void)
{
 /*
  * Return the default printer...
  */

  return (cupsGetDefault2(CUPS_HTTP_DEFAULT));
}


/*
 * 'cupsGetDefault2()' - Get the default printer or class for the specified server.
 *
 * This function returns the default printer or class as defined by
 * the LPDEST or PRINTER environment variables. If these environment
 * variables are not set, the server default destination is returned.
 * Applications should use the @link cupsGetDests@ and @link cupsGetDest@
 * functions to get the user-defined default printer, as this function does
 * not support the lpoptions-defined default printer.
 *
 * @since CUPS 1.1.21/macOS 10.4@ @exclude all@
 */

const char *				/* O - Default printer or @code NULL@ */
cupsGetDefault2(http_t *http)		/* I - Connection to server or @code CUPS_HTTP_DEFAULT@ */
{
  ipp_t		*request,		/* IPP Request */
		*response;		/* IPP Response */
  ipp_attribute_t *attr;		/* Current attribute */
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */


 /*
  * See if we have a user default printer set...
  */

  if (_cupsUserDefault(cg->def_printer, sizeof(cg->def_printer)))
    return (cg->def_printer);

 /*
  * Connect to the server as needed...
  */

  if (!http)
    if ((http = _cupsConnect()) == NULL)
      return (NULL);

 /*
  * Build a CUPS_GET_DEFAULT request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  */

  request = ippNewRequest(IPP_OP_CUPS_GET_DEFAULT);

 /*
  * Do the request and get back a response...
  */

  if ((response = cupsDoRequest(http, request, "/")) != NULL)
  {
    if ((attr = ippFindAttribute(response, "printer-name",
                                 IPP_TAG_NAME)) != NULL)
    {
      strlcpy(cg->def_printer, attr->values[0].string.text,
              sizeof(cg->def_printer));
      ippDelete(response);
      return (cg->def_printer);
    }

    ippDelete(response);
  }

  return (NULL);
}


/*
 * 'cupsGetJobs()' - Get the jobs from the default server.
 *
 * A "whichjobs" value of @code CUPS_WHICHJOBS_ALL@ returns all jobs regardless
 * of state, while @code CUPS_WHICHJOBS_ACTIVE@ returns jobs that are
 * pending, processing, or held and @code CUPS_WHICHJOBS_COMPLETED@ returns
 * jobs that are stopped, canceled, aborted, or completed.
 *
 * @exclude all@
 */

int					/* O - Number of jobs */
cupsGetJobs(cups_job_t **jobs,		/* O - Job data */
            const char *name,		/* I - @code NULL@ = all destinations, otherwise show jobs for named destination */
            int        myjobs,		/* I - 0 = all users, 1 = mine */
	    int        whichjobs)	/* I - @code CUPS_WHICHJOBS_ALL@, @code CUPS_WHICHJOBS_ACTIVE@, or @code CUPS_WHICHJOBS_COMPLETED@ */
{
 /*
  * Return the jobs...
  */

  return (cupsGetJobs2(CUPS_HTTP_DEFAULT, jobs, name, myjobs, whichjobs));
}



/*
 * 'cupsGetJobs2()' - Get the jobs from the specified server.
 *
 * A "whichjobs" value of @code CUPS_WHICHJOBS_ALL@ returns all jobs regardless
 * of state, while @code CUPS_WHICHJOBS_ACTIVE@ returns jobs that are
 * pending, processing, or held and @code CUPS_WHICHJOBS_COMPLETED@ returns
 * jobs that are stopped, canceled, aborted, or completed.
 *
 * @since CUPS 1.1.21/macOS 10.4@
 */

int					/* O - Number of jobs */
cupsGetJobs2(http_t     *http,		/* I - Connection to server or @code CUPS_HTTP_DEFAULT@ */
             cups_job_t **jobs,		/* O - Job data */
             const char *name,		/* I - @code NULL@ = all destinations, otherwise show jobs for named destination */
             int        myjobs,		/* I - 0 = all users, 1 = mine */
	     int        whichjobs)	/* I - @code CUPS_WHICHJOBS_ALL@, @code CUPS_WHICHJOBS_ACTIVE@, or @code CUPS_WHICHJOBS_COMPLETED@ */
{
  int		n;			/* Number of jobs */
  ipp_t		*request,		/* IPP Request */
		*response;		/* IPP Response */
  ipp_attribute_t *attr;		/* Current attribute */
  cups_job_t	*temp;			/* Temporary pointer */
  int		id,			/* job-id */
		priority,		/* job-priority */
		size;			/* job-k-octets */
  ipp_jstate_t	state;			/* job-state */
  time_t	completed_time,		/* time-at-completed */
		creation_time,		/* time-at-creation */
		processing_time;	/* time-at-processing */
  const char	*dest,			/* job-printer-uri */
		*format,		/* document-format */
		*title,			/* job-name */
		*user;			/* job-originating-user-name */
  char		uri[HTTP_MAX_URI];	/* URI for jobs */
  _cups_globals_t *cg = _cupsGlobals();	/* Pointer to library globals */
  static const char * const attrs[] =	/* Requested attributes */
		{
		  "document-format",
		  "job-id",
		  "job-k-octets",
		  "job-name",
		  "job-originating-user-name",
		  "job-printer-uri",
		  "job-priority",
		  "job-state",
		  "time-at-completed",
		  "time-at-creation",
		  "time-at-processing"
		};


 /*
  * Range check input...
  */

  if (!jobs)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);

    return (-1);
  }

 /*
  * Get the right URI...
  */

  if (name)
  {
    if (httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                         "localhost", 0, "/printers/%s",
                         name) < HTTP_URI_STATUS_OK)
    {
      _cupsSetError(IPP_STATUS_ERROR_INTERNAL,
                    _("Unable to create printer-uri"), 1);

      return (-1);
    }
  }
  else
    strlcpy(uri, "ipp://localhost/", sizeof(uri));

  if (!http)
    if ((http = _cupsConnect()) == NULL)
      return (-1);

 /*
  * Build an IPP_GET_JOBS request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  *    requesting-user-name
  *    which-jobs
  *    my-jobs
  *    requested-attributes
  */

  request = ippNewRequest(IPP_OP_GET_JOBS);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
               "printer-uri", NULL, uri);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
               "requesting-user-name", NULL, cupsUser());

  if (myjobs)
    ippAddBoolean(request, IPP_TAG_OPERATION, "my-jobs", 1);

  if (whichjobs == CUPS_WHICHJOBS_COMPLETED)
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                 "which-jobs", NULL, "completed");
  else if (whichjobs == CUPS_WHICHJOBS_ALL)
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                 "which-jobs", NULL, "all");

  ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                "requested-attributes", sizeof(attrs) / sizeof(attrs[0]),
		NULL, attrs);

 /*
  * Do the request and get back a response...
  */

  n     = 0;
  *jobs = NULL;

  if ((response = cupsDoRequest(http, request, "/")) != NULL)
  {
    for (attr = response->attrs; attr; attr = attr->next)
    {
     /*
      * Skip leading attributes until we hit a job...
      */

      while (attr && attr->group_tag != IPP_TAG_JOB)
        attr = attr->next;

      if (!attr)
        break;

     /*
      * Pull the needed attributes from this job...
      */

      id              = 0;
      size            = 0;
      priority        = 50;
      state           = IPP_JSTATE_PENDING;
      user            = "unknown";
      dest            = NULL;
      format          = "application/octet-stream";
      title           = "untitled";
      creation_time   = 0;
      completed_time  = 0;
      processing_time = 0;

      while (attr && attr->group_tag == IPP_TAG_JOB)
      {
        if (!strcmp(attr->name, "job-id") &&
	    attr->value_tag == IPP_TAG_INTEGER)
	  id = attr->values[0].integer;
        else if (!strcmp(attr->name, "job-state") &&
	         attr->value_tag == IPP_TAG_ENUM)
	  state = (ipp_jstate_t)attr->values[0].integer;
        else if (!strcmp(attr->name, "job-priority") &&
	         attr->value_tag == IPP_TAG_INTEGER)
	  priority = attr->values[0].integer;
        else if (!strcmp(attr->name, "job-k-octets") &&
	         attr->value_tag == IPP_TAG_INTEGER)
	  size = attr->values[0].integer;
        else if (!strcmp(attr->name, "time-at-completed") &&
	         attr->value_tag == IPP_TAG_INTEGER)
	  completed_time = attr->values[0].integer;
        else if (!strcmp(attr->name, "time-at-creation") &&
	         attr->value_tag == IPP_TAG_INTEGER)
	  creation_time = attr->values[0].integer;
        else if (!strcmp(attr->name, "time-at-processing") &&
	         attr->value_tag == IPP_TAG_INTEGER)
	  processing_time = attr->values[0].integer;
        else if (!strcmp(attr->name, "job-printer-uri") &&
	         attr->value_tag == IPP_TAG_URI)
	{
	  if ((dest = strrchr(attr->values[0].string.text, '/')) != NULL)
	    dest ++;
        }
        else if (!strcmp(attr->name, "job-originating-user-name") &&
	         attr->value_tag == IPP_TAG_NAME)
	  user = attr->values[0].string.text;
        else if (!strcmp(attr->name, "document-format") &&
	         attr->value_tag == IPP_TAG_MIMETYPE)
	  format = attr->values[0].string.text;
        else if (!strcmp(attr->name, "job-name") &&
	         (attr->value_tag == IPP_TAG_TEXT ||
		  attr->value_tag == IPP_TAG_NAME))
	  title = attr->values[0].string.text;

        attr = attr->next;
      }

     /*
      * See if we have everything needed...
      */

      if (!dest || !id)
      {
        if (!attr)
	  break;
	else
          continue;
      }

     /*
      * Allocate memory for the job...
      */

      if (n == 0)
        temp = malloc(sizeof(cups_job_t));
      else
	temp = realloc(*jobs, sizeof(cups_job_t) * (size_t)(n + 1));

      if (!temp)
      {
       /*
        * Ran out of memory!
        */

        _cupsSetError(IPP_STATUS_ERROR_INTERNAL, NULL, 0);

	cupsFreeJobs(n, *jobs);
	*jobs = NULL;

        ippDelete(response);

	return (-1);
      }

      *jobs = temp;
      temp  += n;
      n ++;

     /*
      * Copy the data over...
      */

      temp->dest            = _cupsStrAlloc(dest);
      temp->user            = _cupsStrAlloc(user);
      temp->format          = _cupsStrAlloc(format);
      temp->title           = _cupsStrAlloc(title);
      temp->id              = id;
      temp->priority        = priority;
      temp->state           = state;
      temp->size            = size;
      temp->completed_time  = completed_time;
      temp->creation_time   = creation_time;
      temp->processing_time = processing_time;

      if (!attr)
        break;
    }

    ippDelete(response);
  }

  if (n == 0 && cg->last_error >= IPP_STATUS_ERROR_BAD_REQUEST)
    return (-1);
  else
    return (n);
}


/*
 * 'cupsGetPrinters()' - Get a list of printers from the default server.
 *
 * This function is deprecated and no longer returns a list of printers - use
 * @link cupsGetDests@ instead.
 *
 * @deprecated@ @exclude all@
 */

int					/* O - Number of printers */
cupsGetPrinters(char ***printers)	/* O - Printers */
{
  if (printers)
    *printers = NULL;

  return (0);
}


/*
 * 'cupsPrintFile()' - Print a file to a printer or class on the default server.
 *
 * @exclude all@
 */

int					/* O - Job ID or 0 on error */
cupsPrintFile(const char    *name,	/* I - Destination name */
              const char    *filename,	/* I - File to print */
	      const char    *title,	/* I - Title of job */
              int           num_options,/* I - Number of options */
	      cups_option_t *options)	/* I - Options */
{
  DEBUG_printf(("cupsPrintFile(name=\"%s\", filename=\"%s\", title=\"%s\", num_options=%d, options=%p)", name, filename, title, num_options, (void *)options));

  return (cupsPrintFiles2(CUPS_HTTP_DEFAULT, name, 1, &filename, title,
                          num_options, options));
}


/*
 * 'cupsPrintFile2()' - Print a file to a printer or class on the specified
 *                      server.
 *
 * @since CUPS 1.1.21/macOS 10.4@ @exclude all@
 */

int					/* O - Job ID or 0 on error */
cupsPrintFile2(
    http_t        *http,		/* I - Connection to server */
    const char    *name,		/* I - Destination name */
    const char    *filename,		/* I - File to print */
    const char    *title,		/* I - Title of job */
    int           num_options,		/* I - Number of options */
    cups_option_t *options)		/* I - Options */
{
  DEBUG_printf(("cupsPrintFile2(http=%p, name=\"%s\", filename=\"%s\",  title=\"%s\", num_options=%d, options=%p)", (void *)http, name, filename, title, num_options, (void *)options));

  return (cupsPrintFiles2(http, name, 1, &filename, title, num_options,
                          options));
}


/*
 * 'cupsPrintFiles()' - Print one or more files to a printer or class on the
 *                      default server.
 *
 * @exclude all@
 */

int					/* O - Job ID or 0 on error */
cupsPrintFiles(
    const char    *name,		/* I - Destination name */
    int           num_files,		/* I - Number of files */
    const char    **files,		/* I - File(s) to print */
    const char    *title,		/* I - Title of job */
    int           num_options,		/* I - Number of options */
    cups_option_t *options)		/* I - Options */
{
  DEBUG_printf(("cupsPrintFiles(name=\"%s\", num_files=%d, files=%p, title=\"%s\", num_options=%d, options=%p)", name, num_files, (void *)files, title, num_options, (void *)options));

 /*
  * Print the file(s)...
  */

  return (cupsPrintFiles2(CUPS_HTTP_DEFAULT, name, num_files, files, title,
                          num_options, options));
}


/*
 * 'cupsPrintFiles2()' - Print one or more files to a printer or class on the
 *                       specified server.
 *
 * @since CUPS 1.1.21/macOS 10.4@ @exclude all@
 */

int					/* O - Job ID or 0 on error */
cupsPrintFiles2(
    http_t        *http,		/* I - Connection to server or @code CUPS_HTTP_DEFAULT@ */
    const char    *name,		/* I - Destination name */
    int           num_files,		/* I - Number of files */
    const char    **files,		/* I - File(s) to print */
    const char    *title,		/* I - Title of job */
    int           num_options,		/* I - Number of options */
    cups_option_t *options)		/* I - Options */
{
  int		i;			/* Looping var */
  int		job_id;			/* New job ID */
  const char	*docname;		/* Basename of current filename */
  const char	*format;		/* Document format */
  cups_file_t	*fp;			/* Current file */
  char		buffer[8192];		/* Copy buffer */
  ssize_t	bytes;			/* Bytes in buffer */
  http_status_t	status;			/* Status of write */
  _cups_globals_t *cg = _cupsGlobals();	/* Global data */
  ipp_status_t	cancel_status;		/* Status code to preserve */
  char		*cancel_message;	/* Error message to preserve */


  DEBUG_printf(("cupsPrintFiles2(http=%p, name=\"%s\", num_files=%d, files=%p, title=\"%s\", num_options=%d, options=%p)", (void *)http, name, num_files, (void *)files, title, num_options, (void *)options));

 /*
  * Range check input...
  */

  if (!name || num_files < 1 || !files)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(EINVAL), 0);

    return (0);
  }

 /*
  * Create the print job...
  */

  if ((job_id = cupsCreateJob(http, name, title, num_options, options)) == 0)
    return (0);

 /*
  * Send each of the files...
  */

  if (cupsGetOption("raw", num_options, options))
    format = CUPS_FORMAT_RAW;
  else if ((format = cupsGetOption("document-format", num_options,
				   options)) == NULL)
    format = CUPS_FORMAT_AUTO;

  for (i = 0; i < num_files; i ++)
  {
   /*
    * Start the next file...
    */

    if ((docname = strrchr(files[i], '/')) != NULL)
      docname ++;
    else
      docname = files[i];

    if ((fp = cupsFileOpen(files[i], "rb")) == NULL)
    {
     /*
      * Unable to open print file, cancel the job and return...
      */

      _cupsSetError(IPP_STATUS_ERROR_DOCUMENT_ACCESS, NULL, 0);
      goto cancel_job;
    }

    status = cupsStartDocument(http, name, job_id, docname, format,
			       i == (num_files - 1));

    while (status == HTTP_STATUS_CONTINUE &&
	   (bytes = cupsFileRead(fp, buffer, sizeof(buffer))) > 0)
      status = cupsWriteRequestData(http, buffer, (size_t)bytes);

    cupsFileClose(fp);

    if (status != HTTP_STATUS_CONTINUE || cupsFinishDocument(http, name) != IPP_STATUS_OK)
    {
     /*
      * Unable to queue, cancel the job and return...
      */

      goto cancel_job;
    }
  }

  return (job_id);

 /*
  * If we get here, something happened while sending the print job so we need
  * to cancel the job without setting the last error (since we need to preserve
  * the current error...
  */

  cancel_job:

  cancel_status  = cg->last_error;
  cancel_message = cg->last_status_message ?
                       _cupsStrRetain(cg->last_status_message) : NULL;

  cupsCancelJob2(http, name, job_id, 0);

  cg->last_error          = cancel_status;
  cg->last_status_message = cancel_message;

  return (0);
}


/*
 * 'cupsStartDocument()' - Add a document to a job created with cupsCreateJob().
 *
 * Use @link cupsWriteRequestData@ to write data for the document and
 * @link cupsFinishDocument@ to finish the document and get the submission status.
 *
 * The MIME type constants @code CUPS_FORMAT_AUTO@, @code CUPS_FORMAT_PDF@,
 * @code CUPS_FORMAT_POSTSCRIPT@, @code CUPS_FORMAT_RAW@, and
 * @code CUPS_FORMAT_TEXT@ are provided for the "format" argument, although
 * any supported MIME type string can be supplied.
 *
 * @since CUPS 1.4/macOS 10.6@ @exclude all@
 */

http_status_t				/* O - HTTP status of request */
cupsStartDocument(
    http_t     *http,			/* I - Connection to server or @code CUPS_HTTP_DEFAULT@ */
    const char *name,			/* I - Destination name */
    int        job_id,			/* I - Job ID from @link cupsCreateJob@ */
    const char *docname,		/* I - Name of document */
    const char *format,			/* I - MIME type or @code CUPS_FORMAT_foo@ */
    int        last_document)		/* I - 1 for last document in job, 0 otherwise */
{
  char		resource[1024],		/* Resource for destination */
		printer_uri[1024];	/* Printer URI */
  ipp_t		*request;		/* Send-Document request */
  http_status_t	status;			/* HTTP status */


 /*
  * Create a Send-Document request...
  */

  if ((request = ippNewRequest(IPP_OP_SEND_DOCUMENT)) == NULL)
  {
    _cupsSetError(IPP_STATUS_ERROR_INTERNAL, strerror(ENOMEM), 0);
    return (HTTP_STATUS_ERROR);
  }

  httpAssembleURIf(HTTP_URI_CODING_ALL, printer_uri, sizeof(printer_uri), "ipp",
                   NULL, "localhost", ippPort(), "/printers/%s", name);
  snprintf(resource, sizeof(resource), "/printers/%s", name);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
               NULL, printer_uri);
  ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", job_id);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());
  if (docname)
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "document-name",
                 NULL, docname);
  if (format)
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE,
                 "document-format", NULL, format);
  ippAddBoolean(request, IPP_TAG_OPERATION, "last-document", (char)last_document);

 /*
  * Send and delete the request, then return the status...
  */

  status = cupsSendRequest(http, request, resource, CUPS_LENGTH_VARIABLE);

  ippDelete(request);

  return (status);
}

static char cups_filter_name[PATH_MAX];
static char cups_filter_path[PATH_MAX];

static int match_filter(const char *fpath, const struct stat *sb, int tflag,
                        struct FTW *ftwbuf) {
  char *filename;
  if (tflag != FTW_F) {
    return 0;
  }
  filename = basename(fpath);
  if (filename) {
    if (strcmp(filename, cups_filter_name) == 0) {
      // filter is matched.
      if (strlen(fpath) < sizeof(cups_filter_path)) {
        strcpy(cups_filter_path, fpath);
        return 1;
      } else {
        return 2;
      }
    }
  }
  return 0;
}

/*
 * '_cupsSearchFilter()' - Recursively search in search_root with a
 * filter_name
 * and set the full_path of its first occurance.
 *
 * return 1 if filter is found,
 * return 2 if buffer is too small,
 * return 0 if filter is not found,
 * return -1 if nftw encounters an error.
 */

/*
 * nftw isn't thread-safe, use pthread mutex to make sure only one thread
 * is executing it.
 */
static pthread_mutex_t nftw_mut = PTHREAD_MUTEX_INITIALIZER;

int _cupsSearchFilter(const char *search_root, const char *filter_name,
                      char *full_path, size_t full_path_size) {
  int status;
  if (strlen(filter_name) >= sizeof(cups_filter_name)) {
    return 2;
  }
  pthread_mutex_lock(&nftw_mut);
  strncpy(cups_filter_name, filter_name, sizeof(cups_filter_name));
  status = nftw(search_root, match_filter, 20, FTW_PHYS);
  if (status == 1) {
    if (full_path_size <= strlen(cups_filter_path)) {
      pthread_mutex_unlock(&nftw_mut);
      return 2;
    }
    strncpy(full_path, cups_filter_path, full_path_size);
  }
  pthread_mutex_unlock(&nftw_mut);
  return status;
}

/*
 * Check if a version is valid version.
 *
 * patterns:
 * X
 * X.Y
 * X.Y.Z
 * X.Y.Z.W
 *
 * For each block (X, Y, etc.), we allow maximum 7 digits and minimum 1 digit.
 *
 * Return 0 if v is a valid version;
 * Otherwise, v is not valid version.
 */

static int checkVersion(const char *v) {
  regex_t re;
  if (regcomp(&re, "^[0-9]{1,7}+(\\.[0-9]{1,7}){0,3}$",
              REG_EXTENDED | REG_NOSUB) == 0) {
    int status = regexec(&re, v, (size_t)0, NULL, 0);
    regfree(&re);
    return status;
  }
  return -1;
}

/*
 *Parse a version_string into version, most significant version number first. If
 *a subversion level doesn't exist, -1 is used.
 *
 *Examples:
 *  "1" -> {1, -1, -1, -1};
 *"1.2" -> {1, 2, -1, -1};
 *"3.1.4.5 -> {3, 1, 4, 5};
 */

static void parseVersion(const char *version_string, int version[4]) {
  int index = sscanf(version_string, "%d.%d.%d.%d", &version[0], &version[1],
                     &version[2], &version[3]);
  while (index < 4) {
    version[index - 1] = -1;
    index++;
  }
}

/*
 * Compare two versions.
 *
 * Return values:
 * 2,  If either v1 or v2 is not valid version
 * -1, If v1 is newer than v2
 * 0,  If v1 is same as v2
 * 1,  If v1 is older than v2
 */

static int compareVersion(const char *v1, const char *v2) {
  int version1[4], version2[4];
  int i = 0;
  if (checkVersion(v1) || checkVersion(v2)) return 2;
  parseVersion(v1, version1);
  parseVersion(v2, version2);
  for (i = 0; i < 4; i++) {
    if (version1[i] > version2[i])
      return -1;
    else if (version1[i] < version2[i])
      return 1;
  }
  return 0;
}

/*
 * 'searchFilterLatest()' - Recursively search in search_root with a filter_name
 * for its latest version and set the full_path of its first occurance.
 *
 * Dir structure:
 *  /<version 1>/...
 *  /<version 2>/...
 *  ...
 *
 * return 1 if filter is found,
 * return 2 if string buffer is too small,
 * return 0 if filter is not found,
 * return -1 if nftw encounters an error.
 */

int searchFilterLatest(const char *search_root, const char *filter_name,
                       char *full_path, size_t full_path_size) {
  DIR *dir = NULL;
  struct dirent *ent = NULL;
  char latest_version[PATH_MAX];
  char latest_version_search_root[PATH_MAX];
  int status = 0;
  if (!(dir = opendir(search_root))) return -1;
  latest_version[0] = '\0';
  while ((ent = readdir(dir))) {
    if (ent->d_type == DT_DIR && checkVersion(ent->d_name) == 0) {
      /*
       * If any version entry is too large to handle, then we terminate
       * immediately.
       */
      if (strlen(ent->d_name) >= sizeof(latest_version)) return 2;
      if (latest_version[0] == '\0')
        strncpy(latest_version, ent->d_name, sizeof(latest_version));
      else if (compareVersion(latest_version, ent->d_name) == 1)
        strncpy(latest_version, ent->d_name, sizeof(latest_version));
    }
  }
  if (latest_version[0] != '\0') {
    if (sizeof(latest_version_search_root) >=
        strlen(search_root) + strlen(latest_version) +
            3 /* 3: sizeof('/') * 2 + sizeof('\0'); */) {
      sprintf(latest_version_search_root, "%s/%s/", search_root,
              latest_version);
      status = _cupsSearchFilter(latest_version_search_root, filter_name,
                                 full_path, full_path_size);
    }
  }
  closedir(dir);
  return status;
}

#define COMPONENT_FILTERS_LENGTH 4
/* a map from filter name to component name */
static const char *component_filters[COMPONENT_FILTERS_LENGTH][2] = {
    {"epson-escpr-wrapper", "epson-inkjet-printer-escpr"},
    {"epson-escpr", "epson-inkjet-printer-escpr"},
    {"rastertostar", "star-cups-driver"},
    {"rastertostarlm", "star-cups-driver"}};

/*
 * '_cupsSearchFilterLatest()' - Identify component folder for a filter. Then
 * recursively search in the component's root folder with a filter_name for its
 * latest version and set the full_path of its first occurance.
 *
 * Dir structure:
 *  /<version 1>/...
 *  /<version 2>/...
 *  ...
 *
 * return 1 if filter is found,
 * return 2 if buffer is too small,
 * return 3 if component does not exist for this filter.
 * return 0 if filter is not found,
 * return -1 if nftw encounters an error.
 */

int _cupsSearchFilterLatest(const char *filter_name, char *full_path,
                            size_t full_path_size) {
  int i;
  for (i = 0; i < COMPONENT_FILTERS_LENGTH; i++) {
    const char *filter = component_filters[i][0];
    const char *component = component_filters[i][1];
    if (strcmp(filter_name, filter) == 0) {
      char search_root[PATH_MAX];
      strcpy(search_root, "/run/imageloader/");
      strcat(search_root, component);
      return searchFilterLatest(search_root, filter, full_path, full_path_size);
    }
  }
  return 3;
}

/* See cups-private.h for description. */
ssize_t _cupsWriteWrapper(const int filedes, const void *buffer, const size_t size) {
  EC_FUNC;

  /* The maximum number of allowed "soft" write(...) failures in a row. */
  const int max_number_of_failures = 8;
  int number_of_failures = 0;

  for (size_t to_write = size; to_write > 0; ) {
    const ssize_t written = write(filedes, buffer, to_write);

    /* Checking for errors. */
    if (written < 0) {
      if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
        /* We can try to write again; but the function
         * must fail if we reached the limit of failing calls. */
        if (++number_of_failures > max_number_of_failures)
          RETURN_FAIL_UNKNOWN(-1);

        /* Wait 20ms and try to call write(...) again. */
        usleep(20*1000);
        continue;
      } else if (errno == EIO || errno == ENOSPC) {
        /* We cannot deal with this error, the function must fail.*/
        RETURN_FAIL_IO(-1);
      } else {
        /* We cannot deal with this error, the function must fail.*/
        RETURN_FAIL_UNKNOWN(-1);
      }
    }

    /* A chunk of data was written successfully. */
    buffer += written;
    to_write -= (size_t)written;
    number_of_failures = 0;
  }

  /* Everything was written successfully - success. */
  RETURN_OK((ssize_t)size);
}
