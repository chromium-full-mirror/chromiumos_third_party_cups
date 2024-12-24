/*
 * "lpadmin" command for CUPS.
 *
 * Copyright © 2020-2024 by OpenPrinting.
 * Copyright © 2007-2021 by Apple Inc.
 * Copyright © 1997-2006 by Easy Software Products.
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include <cups/cups-private.h>
#include <cups/error-codes.h>
#include <cups/ippusb-private.h>
#include <cups/ppd-private.h>

/*
 * Codes returned by lpadmin.
 */

typedef enum lpadmin_status_e {
	LPAS_OK = 0,
	LPAS_UNKNOWN_ERROR = 1,
	LPAS_WRONG_PARAMETERS = 2,
	LPAS_IO_ERROR = 3,
	LPAS_MEMORY_ALLOC_ERROR = 4,
	LPAS_INVALID_PPD_FILE = 5,
	LPAS_SERVER_UNREACHABLE = 6,
	LPAS_PRINTER_UNREACHABLE = 7,
	LPAS_PRINTER_WRONG_RESPONSE = 8,
	LPAS_PRINTER_NOT_AUTOCONFIGURABLE = 9
} lpadmin_status_t;

/*
 * Local functions...
 */

static int		add_printer_to_class(http_t *http, char *printer, char *pclass);
static int		default_printer(http_t *http, char *printer);
static int		delete_printer(http_t *http, char *printer);
static int		delete_printer_from_class(http_t *http, char *printer,
			                          char *pclass);
static int		delete_printer_option(http_t *http, char *printer,
			                      char *option);
static int		enable_printer(http_t *http, char *printer);
lpadmin_status_t get_printer_ppd(const char *uri, char *buffer, size_t bufsize, int *num_options, cups_option_t **options);
static cups_ptype_t	get_printer_type(http_t *http, char *printer, char *uri,
			                 size_t urisize);
static int		set_printer_options(http_t *http, char *printer,
			                    int num_options, cups_option_t *options,
					    char *file, int enable);
static void		usage(void) _CUPS_NORETURN;
static int		validate_name(const char *name);

static ipp_t *request_document_formats(http_t *http, const char *resource,
                                       const char *uri);

static lpadmin_status_t status_from_last_error_code();

/*
 * 'main()' - Parse options and configure the scheduler.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
  int		i;			/* Looping var */
  http_t	*http;			/* Connection to server */
  char		*printer,		/* Destination printer */
		*pclass,		/* Printer class name */
		*opt,			/* Option pointer */
		*val;			/* Pointer to allow/deny value */
  int		enable = 0;		/* Enable/resume printer? */
  int		num_options;		/* Number of options */
  cups_option_t	*options;		/* Options */
  char		*file,			/* New PPD file */
		evefile[1024] = "";	/* IPP Everywhere PPD */
  const char	*ppd_name,		/* ppd-name value */
		*device_uri;		/* device-uri value */


  _cupsSetLocale(argv);

  http        = NULL;
  printer     = NULL;
  num_options = 0;
  options     = NULL;
  file        = NULL;

  for (i = 1; i < argc; i ++)
  {
    if (!strcmp(argv[i], "--help"))
      usage();
    else if (argv[i][0] == '-')
    {
      for (opt = argv[i] + 1; *opt; opt ++)
      {
	switch (*opt)
	{
	  case 'c' : /* Add printer to class */
	      if (!http)
	      {
		http = httpConnect2(cupsServer(), ippPort(), NULL, AF_UNSPEC, cupsEncryption(), 1, 30000, NULL);

		if (http == NULL)
		{
		  _cupsLangPrintf(stderr, _("lpadmin: Unable to connect to server: %s"), strerror(errno));
		  return LPAS_SERVER_UNREACHABLE;
		}
	      }

	      if (printer == NULL)
	      {
		_cupsLangPuts(stderr,
			      _("lpadmin: Unable to add a printer to the class:\n"
				"         You must specify a printer name first."));
		return LPAS_WRONG_PARAMETERS;
	      }

	      if (opt[1] != '\0')
	      {
		pclass = opt + 1;
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPuts(stderr, _("lpadmin: Expected class name after \"-c\" option."));
		  usage();
		}

		pclass = argv[i];
	      }

	      if (!validate_name(pclass))
	      {
		_cupsLangPuts(stderr,
			      _("lpadmin: Class name can only contain printable "
				"characters."));
		return LPAS_WRONG_PARAMETERS;
	      }

	      if (add_printer_to_class(http, printer, pclass))
		return LPAS_UNKNOWN_ERROR;
	      break;

	  case 'd' : /* Set as default destination */
	      if (!http)
	      {
		http = httpConnect2(cupsServer(), ippPort(), NULL, AF_UNSPEC, cupsEncryption(), 1, 30000, NULL);

		if (http == NULL)
		{
		  _cupsLangPrintf(stderr, _("lpadmin: Unable to connect to server: %s"), strerror(errno));
		  return LPAS_SERVER_UNREACHABLE;
		}
	      }

	      if (opt[1] != '\0')
	      {
		printer = opt + 1;
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPuts(stderr, _("lpadmin: Expected printer name after \"-d\" option."));
		  usage();
		}

		printer = argv[i];
	      }

	      if (!validate_name(printer))
	      {
		_cupsLangPuts(stderr, _("lpadmin: Printer name can only contain printable characters."));
		return LPAS_WRONG_PARAMETERS;
	      }

	      if (default_printer(http, printer))
		return LPAS_UNKNOWN_ERROR;

	      i = argc;
	      break;

	  case 'h' : /* Connect to host */
	      if (http)
	      {
		httpClose(http);
		http = NULL;
	      }

	      if (opt[1] != '\0')
	      {
		cupsSetServer(opt + 1);
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPuts(stderr, _("lpadmin: Expected hostname after \"-h\" option."));
		  usage();
		}

		cupsSetServer(argv[i]);
	      }
	      break;

	  case 'P' : /* Use the specified PPD file */
	  case 'i' : /* Use the specified PPD file */
	      if (opt[1] != '\0')
	      {
		file = opt + 1;
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPrintf(stderr, _("lpadmin: Expected PPD after \"-%c\" option."), argv[i - 1][1]);
		  usage();
		}

		file = argv[i];
	      }

	      if (*opt == 'i')
	      {
	       /*
	        * Check to see that the specified file is, in fact, a PPD...
	        */

                cups_file_t *fp = cupsFileOpen(file, "r");
                char line[256];

                if (!cupsFileGets(fp, line, sizeof(line)) || strncmp(line, "*PPD-Adobe", 10))
                {
                  _cupsLangPuts(stderr, _("lpadmin: System V interface scripts are no longer supported for security reasons."));
                  cupsFileClose(fp);
                  return LPAS_INVALID_PPD_FILE;
                }

                cupsFileClose(fp);
	      }
	      break;

	  case 'E' : /* Enable the printer/enable encryption */
	      if (printer == NULL)
	      {
#ifdef HAVE_TLS
		cupsSetEncryption(HTTP_ENCRYPTION_REQUIRED);

		if (http)
		  httpEncryption(http, HTTP_ENCRYPTION_REQUIRED);
#else
		_cupsLangPrintf(stderr, _("%s: Sorry, no encryption support."), argv[0]);
#endif /* HAVE_TLS */
		break;
	      }

	      if (!http)
	      {
		http = httpConnect2(cupsServer(), ippPort(), NULL, AF_UNSPEC, cupsEncryption(), 1, 30000, NULL);

		if (http == NULL)
		{
		  _cupsLangPrintf(stderr,
				  _("lpadmin: Unable to connect to server: %s"),
				  strerror(errno));
		  return LPAS_SERVER_UNREACHABLE;
		}
	      }

              enable = 1;
	      break;

	  case 'm' : /* Use the specified standard script/PPD file */
	      if (opt[1] != '\0')
	      {
		num_options = cupsAddOption("ppd-name", opt + 1, num_options, &options);
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPuts(stderr, _("lpadmin: Expected model after \"-m\" option."));
		  usage();
		}

		num_options = cupsAddOption("ppd-name", argv[i], num_options, &options);
	      }
	      break;

	  case 'o' : /* Set option */
	      if (opt[1] != '\0')
	      {
		num_options = cupsParseOptions(opt + 1, num_options, &options);
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPuts(stderr, _("lpadmin: Expected name=value after \"-o\" option."));
		  usage();
		}

		num_options = cupsParseOptions(argv[i], num_options, &options);
	      }
	      break;

	  case 'p' : /* Add/modify a printer */
	      if (opt[1] != '\0')
	      {
		printer = opt + 1;
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPuts(stderr, _("lpadmin: Expected printer after \"-p\" option."));
		  usage();
		}

		printer = argv[i];
	      }

	      if (!validate_name(printer))
	      {
		_cupsLangPuts(stderr, _("lpadmin: Printer name can only contain printable characters."));
		return LPAS_WRONG_PARAMETERS;
	      }
	      break;

	  case 'r' : /* Remove printer from class */
	      if (!http)
	      {
		http = httpConnect2(cupsServer(), ippPort(), NULL, AF_UNSPEC, cupsEncryption(), 1, 30000, NULL);

		if (http == NULL)
		{
		  _cupsLangPrintf(stderr,
				  _("lpadmin: Unable to connect to server: %s"),
				  strerror(errno));
		  return LPAS_SERVER_UNREACHABLE;
		}
	      }

	      if (printer == NULL)
	      {
		_cupsLangPuts(stderr,
			      _("lpadmin: Unable to remove a printer from the class:\n"
				"         You must specify a printer name first."));
		return LPAS_WRONG_PARAMETERS;
	      }

	      if (opt[1] != '\0')
	      {
		pclass = opt + 1;
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPuts(stderr, _("lpadmin: Expected class after \"-r\" option."));
		  usage();
		}

		pclass = argv[i];
	      }

	      if (!validate_name(pclass))
	      {
		_cupsLangPuts(stderr, _("lpadmin: Class name can only contain printable characters."));
		return LPAS_WRONG_PARAMETERS;
	      }

	      if (delete_printer_from_class(http, printer, pclass))
		return LPAS_UNKNOWN_ERROR;
	      break;

	  case 'R' : /* Remove option */
	      if (!http)
	      {
		http = httpConnect2(cupsServer(), ippPort(), NULL, AF_UNSPEC, cupsEncryption(), 1, 30000, NULL);

		if (http == NULL)
		{
		  _cupsLangPrintf(stderr, _("lpadmin: Unable to connect to server: %s"), strerror(errno));
		  return LPAS_SERVER_UNREACHABLE;
		}
	      }

	      if (printer == NULL)
	      {
		_cupsLangPuts(stderr,
			      _("lpadmin: Unable to delete option:\n"
				"         You must specify a printer name first."));
		return LPAS_WRONG_PARAMETERS;
	      }

	      if (opt[1] != '\0')
	      {
		val = opt + 1;
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPuts(stderr, _("lpadmin: Expected name after \"-R\" option."));
		  usage();
		}

		val = argv[i];
	      }

	      if (delete_printer_option(http, printer, val))
		return LPAS_UNKNOWN_ERROR;
	      break;

	  case 'U' : /* Username */
	      if (opt[1] != '\0')
	      {
		cupsSetUser(opt + 1);
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;
		if (i >= argc)
		{
		  _cupsLangPrintf(stderr, _("%s: Error - expected username after \"-U\" option."), argv[0]);
		  usage();
		}

		cupsSetUser(argv[i]);
	      }
	      break;

	  case 'u' : /* Allow/deny users */
	      if (opt[1] != '\0')
	      {
		val = opt + 1;
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPuts(stderr, _("lpadmin: Expected allow/deny:userlist after \"-u\" option."));
		  usage();
		}

		val = argv[i];
	      }

	      if (!_cups_strncasecmp(val, "allow:", 6))
		num_options = cupsAddOption("requesting-user-name-allowed", val + 6, num_options, &options);
	      else if (!_cups_strncasecmp(val, "deny:", 5))
		num_options = cupsAddOption("requesting-user-name-denied", val + 5, num_options, &options);
	      else
	      {
		_cupsLangPrintf(stderr, _("lpadmin: Unknown allow/deny option \"%s\"."), val);
		return LPAS_WRONG_PARAMETERS;
	      }
	      break;

	  case 'v' : /* Set the device-uri attribute */
	      if (opt[1] != '\0')
	      {
		num_options = cupsAddOption("device-uri", opt + 1, num_options, &options);
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPuts(stderr, _("lpadmin: Expected device URI after \"-v\" option."));
		  usage();
		}

		num_options = cupsAddOption("device-uri", argv[i], num_options, &options);
	      }
	      break;

	  case 'x' : /* Delete a printer */
	      if (!http)
	      {
		http = httpConnect2(cupsServer(), ippPort(), NULL, AF_UNSPEC, cupsEncryption(), 1, 30000, NULL);

		if (http == NULL)
		{
		  _cupsLangPrintf(stderr,
				  _("lpadmin: Unable to connect to server: %s"),
				  strerror(errno));
		  return LPAS_SERVER_UNREACHABLE;
		}
	      }

	      if (opt[1] != '\0')
	      {
		printer = opt + 1;
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPuts(stderr, _("lpadmin: Expected printer or class after \"-x\" option."));
		  usage();
		}

		printer = argv[i];
	      }

	      if (!validate_name(printer))
	      {
		_cupsLangPuts(stderr, _("lpadmin: Printer name can only contain printable characters."));
		return LPAS_WRONG_PARAMETERS;
	      }

	      if (delete_printer(http, printer))
		return LPAS_UNKNOWN_ERROR;

	      i = argc;
	      break;

	  case 'D' : /* Set the printer-info attribute */
	      if (opt[1] != '\0')
	      {
		num_options = cupsAddOption("printer-info", opt + 1, num_options, &options);
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPuts(stderr, _("lpadmin: Expected description after \"-D\" option."));
		  usage();
		}

		num_options = cupsAddOption("printer-info", argv[i], num_options, &options);
	      }
	      break;

	  case 'I' : /* Set the supported file types (ignored) */
	      i ++;

	      if (i >= argc)
	      {
		_cupsLangPuts(stderr, _("lpadmin: Expected file type(s) after \"-I\" option."));
		usage();
	      }

	      _cupsLangPuts(stderr, _("lpadmin: Warning - content type list ignored."));
	      break;

	  case 'L' : /* Set the printer-location attribute */
	      if (opt[1] != '\0')
	      {
		num_options = cupsAddOption("printer-location", opt + 1, num_options, &options);
		opt += strlen(opt) - 1;
	      }
	      else
	      {
		i ++;

		if (i >= argc)
		{
		  _cupsLangPuts(stderr, _("lpadmin: Expected location after \"-L\" option."));
		  usage();
		}

		num_options = cupsAddOption("printer-location", argv[i], num_options, &options);
	      }
	      break;

	  default :
	      _cupsLangPrintf(stderr, _("lpadmin: Unknown option \"%c\"."), *opt);
	      usage();
	}
      }
    }
    else
    {
      _cupsLangPrintf(stderr, _("lpadmin: Unknown argument \"%s\"."), argv[i]);
      usage();
    }
  }

 /*
  * Set options as needed...
  */

  ppd_name   = cupsGetOption("ppd-name", num_options, options);
  device_uri = cupsGetOption("device-uri", num_options, options);

  if (ppd_name && !strcmp(ppd_name, "raw"))
  {
#ifdef __APPLE__
    _cupsLangPuts(stderr, _("lpadmin: Raw queues are no longer supported on macOS."));
#else
    _cupsLangPuts(stderr, _("lpadmin: Raw queues are deprecated and will stop working in a future version of CUPS."));
#endif /* __APPLE__ */

    if (device_uri && (!strncmp(device_uri, "ipp://", 6) || !strncmp(device_uri, "ipps://", 7)) && strstr(device_uri, "/printers/"))
      _cupsLangPuts(stderr, _("lpadmin: Use the 'everywhere' model for shared printers."));

#ifdef __APPLE__
    return (1);
#endif /* __APPLE__ */
  }
  else if (ppd_name && !strcmp(ppd_name, "everywhere") && device_uri)
  {
    lpadmin_status_t status;
    if ((status = get_printer_ppd(device_uri, evefile, sizeof(evefile), &num_options, &options)) != LPAS_OK)
      return (int)status;
    file = evefile;
    num_options = cupsRemoveOption("ppd-name", num_options, &options);
  }

  if (num_options || file)
  {
    if (printer == NULL)
    {
      _cupsLangPuts(stderr,
                    _("lpadmin: Unable to set the printer options:\n"
                      "         You must specify a printer name first."));
      return LPAS_WRONG_PARAMETERS;
    }

    if (!http)
    {
      http = httpConnect2(cupsServer(), ippPort(), NULL, AF_UNSPEC,
                          cupsEncryption(), 1, 30000, NULL);

      if (http == NULL) {
        _cupsLangPrintf(stderr, _("lpadmin: Unable to connect to server: %s"),
                        strerror(errno));
        return LPAS_SERVER_UNREACHABLE;
      }
    }

    if (set_printer_options(http, printer, num_options, options, file, enable))
      return LPAS_UNKNOWN_ERROR;
  }
  else if (enable && enable_printer(http, printer))
    return LPAS_UNKNOWN_ERROR;

  if (evefile[0])
    unlink(evefile);

  if (printer == NULL)
    usage();

  if (http)
    httpClose(http);

  return LPAS_OK;
}


/*
 * 'add_printer_to_class()' - Add a printer to a class.
 */

static int				/* O - 0 on success, 1 on fail */
add_printer_to_class(http_t *http,	/* I - Server connection */
                     char   *printer,	/* I - Printer to add */
		     char   *pclass)	/* I - Class to add to */
{
  int		i;			/* Looping var */
  ipp_t		*request,		/* IPP Request */
		*response;		/* IPP Response */
  ipp_attribute_t *attr,		/* Current attribute */
		*members;		/* Members in class */
  char		uri[HTTP_MAX_URI];	/* URI for printer/class */


 /*
  * Build an IPP_OP_GET_PRINTER_ATTRIBUTES request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  *    requesting-user-name
  */

  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                   "localhost", 0, "/classes/%s", pclass);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
               "printer-uri", NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());

 /*
  * Do the request and get back a response...
  */

  response = cupsDoRequest(http, request, "/");

 /*
  * Build a CUPS-Add-Modify-Class request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  *    requesting-user-name
  *    member-uris
  */

  request = ippNewRequest(IPP_OP_CUPS_ADD_MODIFY_CLASS);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
               "printer-uri", NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());

 /*
  * See if the printer is already in the class...
  */

  if (response != NULL &&
      (members = ippFindAttribute(response, "member-names",
                                  IPP_TAG_NAME)) != NULL)
    for (i = 0; i < members->num_values; i ++)
      if (_cups_strcasecmp(printer, members->values[i].string.text) == 0)
      {
        _cupsLangPrintf(stderr,
	                _("lpadmin: Printer %s is already a member of class "
			  "%s."), printer, pclass);
        ippDelete(request);
	ippDelete(response);
	return (0);
      }

 /*
  * OK, the printer isn't part of the class, so add it...
  */

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                   "localhost", 0, "/printers/%s", printer);

  if (response != NULL &&
      (members = ippFindAttribute(response, "member-uris",
                                  IPP_TAG_URI)) != NULL)
  {
   /*
    * Add the printer to the existing list...
    */

    attr = ippAddStrings(request, IPP_TAG_PRINTER, IPP_TAG_URI,
                         "member-uris", members->num_values + 1, NULL, NULL);
    for (i = 0; i < members->num_values; i ++)
      attr->values[i].string.text =
          _cupsStrAlloc(members->values[i].string.text);

    attr->values[i].string.text = _cupsStrAlloc(uri);
  }
  else
    ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_URI, "member-uris", NULL,
                 uri);

 /*
  * Then send the request...
  */

  ippDelete(response);

  ippDelete(cupsDoRequest(http, request, "/admin/"));
  if (cupsLastError() > IPP_STATUS_OK_CONFLICTING)
  {
    _cupsLangPrintf(stderr, _("%s: %s"), "lpadmin", cupsLastErrorString());

    return (1);
  }
  else
    return (0);
}


/*
 * 'default_printer()' - Set the default printing destination.
 */

static int				/* O - 0 on success, 1 on fail */
default_printer(http_t *http,		/* I - Server connection */
                char   *printer)	/* I - Printer name */
{
  ipp_t		*request;		/* IPP Request */
  char		uri[HTTP_MAX_URI];	/* URI for printer/class */


 /*
  * Build a CUPS-Set-Default request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  *    requesting-user-name
  */

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                   "localhost", 0, "/printers/%s", printer);

  request = ippNewRequest(IPP_OP_CUPS_SET_DEFAULT);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
               "printer-uri", NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());

 /*
  * Do the request and get back a response...
  */

  ippDelete(cupsDoRequest(http, request, "/admin/"));

  if (cupsLastError() > IPP_STATUS_OK_CONFLICTING)
  {
    _cupsLangPrintf(stderr, _("%s: %s"), "lpadmin", cupsLastErrorString());

    return (1);
  }
  else
    return (0);
}


/*
 * 'delete_printer()' - Delete a printer from the system...
 */

static int				/* O - 0 on success, 1 on fail */
delete_printer(http_t *http,		/* I - Server connection */
               char   *printer)		/* I - Printer to delete */
{
  ipp_t		*request;		/* IPP Request */
  char		uri[HTTP_MAX_URI];	/* URI for printer/class */


 /*
  * Build a CUPS-Delete-Printer request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  *    requesting-user-name
  */

  request = ippNewRequest(IPP_OP_CUPS_DELETE_PRINTER);

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                   "localhost", 0, "/printers/%s", printer);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
               "printer-uri", NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());

 /*
  * Do the request and get back a response...
  */

  ippDelete(cupsDoRequest(http, request, "/admin/"));

  if (cupsLastError() > IPP_STATUS_OK_CONFLICTING)
  {
    _cupsLangPrintf(stderr, _("%s: %s"), "lpadmin", cupsLastErrorString());

    return (1);
  }
  else
    return (0);
}


/*
 * 'delete_printer_from_class()' - Delete a printer from a class.
 */

static int				/* O - 0 on success, 1 on fail */
delete_printer_from_class(
    http_t *http,			/* I - Server connection */
    char   *printer,			/* I - Printer to remove */
    char   *pclass)	  		/* I - Class to remove from */
{
  int		i, j, k;		/* Looping vars */
  ipp_t		*request,		/* IPP Request */
		*response;		/* IPP Response */
  ipp_attribute_t *attr,		/* Current attribute */
		*members;		/* Members in class */
  char		uri[HTTP_MAX_URI];	/* URI for printer/class */


 /*
  * Build an IPP_OP_GET_PRINTER_ATTRIBUTES request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  *    requesting-user-name
  */

  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL,
                   "localhost", 0, "/classes/%s", pclass);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
               "printer-uri", NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());

 /*
  * Do the request and get back a response...
  */

  if ((response = cupsDoRequest(http, request, "/classes/")) == NULL ||
      response->request.status.status_code == IPP_STATUS_ERROR_NOT_FOUND)
  {
    _cupsLangPrintf(stderr, _("%s: %s"), "lpadmin", cupsLastErrorString());

    ippDelete(response);

    return (1);
  }

 /*
  * See if the printer is already in the class...
  */

  if ((members = ippFindAttribute(response, "member-names", IPP_TAG_NAME)) == NULL)
  {
    _cupsLangPuts(stderr, _("lpadmin: No member names were seen."));

    ippDelete(response);

    return (1);
  }

  for (i = 0; i < members->num_values; i ++)
    if (!_cups_strcasecmp(printer, members->values[i].string.text))
      break;

  if (i >= members->num_values)
  {
    _cupsLangPrintf(stderr,
                    _("lpadmin: Printer %s is not a member of class %s."),
	            printer, pclass);

    ippDelete(response);

    return (1);
  }

  if (members->num_values == 1)
  {
   /*
    * Build a CUPS-Delete-Class request, which requires the following
    * attributes:
    *
    *    attributes-charset
    *    attributes-natural-language
    *    printer-uri
    *    requesting-user-name
    */

    request = ippNewRequest(IPP_OP_CUPS_DELETE_CLASS);

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
        	 "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
                 "requesting-user-name", NULL, cupsUser());
  }
  else
  {
   /*
    * Build a IPP_OP_CUPS_ADD_MODIFY_CLASS request, which requires the following
    * attributes:
    *
    *    attributes-charset
    *    attributes-natural-language
    *    printer-uri
    *    requesting-user-name
    *    member-uris
    */

    request = ippNewRequest(IPP_OP_CUPS_ADD_MODIFY_CLASS);

    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
        	 "printer-uri", NULL, uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
                 "requesting-user-name", NULL, cupsUser());

   /*
    * Delete the printer from the class...
    */

    members = ippFindAttribute(response, "member-uris", IPP_TAG_URI);
    attr = ippAddStrings(request, IPP_TAG_PRINTER, IPP_TAG_URI,
                         "member-uris", members->num_values - 1, NULL, NULL);

    for (j = 0, k = 0; j < members->num_values; j ++)
      if (j != i)
        attr->values[k ++].string.text =
	    _cupsStrAlloc(members->values[j].string.text);
  }

 /*
  * Then send the request...
  */

  ippDelete(response);

  ippDelete(cupsDoRequest(http, request, "/admin/"));

  if (cupsLastError() > IPP_STATUS_OK_CONFLICTING)
  {
    _cupsLangPrintf(stderr, _("%s: %s"), "lpadmin", cupsLastErrorString());

    return (1);
  }
  else
    return (0);
}


/*
 * 'delete_printer_option()' - Delete a printer option.
 */

static int				/* O - 0 on success, 1 on fail */
delete_printer_option(http_t *http,	/* I - Server connection */
                      char   *printer,	/* I - Printer */
		      char   *option)	/* I - Option to delete */
{
  ipp_t		*request;		/* IPP request */
  char		uri[HTTP_MAX_URI];	/* URI for printer/class */


 /*
  * Build a IPP_OP_CUPS_ADD_MODIFY_PRINTER or IPP_OP_CUPS_ADD_MODIFY_CLASS request, which
  * requires the following attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  *    requesting-user-name
  *    option with deleteAttr tag
  */

  if (get_printer_type(http, printer, uri, sizeof(uri)) & CUPS_PRINTER_CLASS)
    request = ippNewRequest(IPP_OP_CUPS_ADD_MODIFY_CLASS);
  else
    request = ippNewRequest(IPP_OP_CUPS_ADD_MODIFY_PRINTER);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
               "printer-uri", NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
               "requesting-user-name", NULL, cupsUser());
  ippAddInteger(request, IPP_TAG_PRINTER, IPP_TAG_DELETEATTR, option, 0);

 /*
  * Do the request and get back a response...
  */

  ippDelete(cupsDoRequest(http, request, "/admin/"));

  if (cupsLastError() > IPP_STATUS_OK_CONFLICTING)
  {
    _cupsLangPrintf(stderr, _("%s: %s"), "lpadmin", cupsLastErrorString());

    return (1);
  }
  else
    return (0);
}


/*
 * 'enable_printer()' - Enable a printer...
 */

static int				/* O - 0 on success, 1 on fail */
enable_printer(http_t *http,		/* I - Server connection */
               char   *printer)		/* I - Printer to enable */
{
  ipp_t		*request;		/* IPP Request */
  char		uri[HTTP_MAX_URI];	/* URI for printer/class */


 /*
  * Send IPP_OP_ENABLE_PRINTER and IPP_OP_RESUME_PRINTER requests, which
  * require the following attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  *    requesting-user-name
  */

  request = ippNewRequest(IPP_OP_ENABLE_PRINTER);

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), "ipp", NULL, "localhost", ippPort(), "/printers/%s", printer);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  ippDelete(cupsDoRequest(http, request, "/admin/"));

  if (cupsLastError() > IPP_STATUS_OK_CONFLICTING)
  {
    _cupsLangPrintf(stderr, _("%s: %s"), "lpadmin", cupsLastErrorString());

    return (1);
  }

  request = ippNewRequest(IPP_OP_RESUME_PRINTER);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

  ippDelete(cupsDoRequest(http, request, "/admin/"));

  if (cupsLastError() > IPP_STATUS_OK_CONFLICTING)
  {
    _cupsLangPrintf(stderr, _("%s: %s"), "lpadmin", cupsLastErrorString());

    return (1);
  }

  return (0);
}


/*
 * 'get_closest_language()' - Get the language supported by the printer that is closest to the user's locale.
 *
 * The lifetime of the returned string is equal to that of the "languages" parameter.
 */

static const char *					/* O  - the closest supported language */
get_closest_language(ipp_attribute_t *languages)	/* I  - list of supported languages */
{
  const char *desired_language = cupsLangDefault()->language;
  const char *closest = NULL;
  int closest_distance = 9999;
  int language_count = ippGetCount(languages);

  for (int i = 0; i < language_count; i++)
  {
    const char *language = ippGetString(languages, i, NULL);
    int distance = 9999;
    if (!_cups_strcasecmp(language, desired_language))
    {
      // same string
      distance = 0;
    }
    else if (!_cups_strncasecmp(language, desired_language, 2))
    {
      if (strlen(language) > 3 && strlen(desired_language) > 3 &&
          !_cups_strcasecmp(language + 3, desired_language + 3))
      {
        // identical aside from hyphen/underscore or caps (en-us/en_US)
        distance = 0;
      }
      else
      {
        // same language, different country code (en-us/en-gb or es/es-mx)
        distance = 1;
      }
    }
    else
    {
      // completely different languages
      // fall back on English if there's no match for the user's language
      distance = _cups_strncasecmp(language, "en", 2) ? 3 : 2;
    }

    if (distance < closest_distance)
    {
      closest_distance = distance;
      closest = language;
    }
  }

  return closest;
}


// Constructs an ippusb:// URL with the hostname of `uri_from_hostname` and the
// resource path of `uri_with_resource`. For example:
//    uri_with_hostname: ipp://03f0_0d74/ipp/print
//    uri_with_resource: http://localhost:60000/en.strings
//    -> result_uri: ippusb://03f0_0d74/en.strings
// This function can be removed when we move away from domain sockets for IPP-USB.
int url_to_ippusb_host(const char *uri_with_hostname, const char* uri_with_resource,
                       char* result_uri, size_t uri_buffer_size) {
  char hostname[1024],
       resource[1024],
       scheme_unused[256],
       hostname_unused[1024],
       username_unused[256],
       resource_unused[1024];
  int port_unused;
  http_uri_status_t result;
  int result_size;

  result = httpSeparateURI(HTTP_URI_CODING_ALL, uri_with_hostname,
                  scheme_unused, sizeof(scheme_unused),
                  username_unused, sizeof(username_unused),
                  hostname, sizeof(hostname),
                  &port_unused,
                  resource_unused, sizeof(resource_unused));
  if (result != HTTP_URI_STATUS_OK) {
    _cupsLangPrintf(stderr, _("Failed to decode URI \"%s\""), uri_with_hostname);
    return 0;
  }

  result = httpSeparateURI(HTTP_URI_CODING_ALL, uri_with_resource,
                  scheme_unused, sizeof(scheme_unused),
                  username_unused, sizeof(username_unused),
                  hostname_unused, sizeof(hostname_unused),
                  &port_unused,
                  resource, sizeof(resource));
  if (result != HTTP_URI_STATUS_OK) {
    _cupsLangPrintf(stderr, _("Failed to decode URI \"%s\""), uri_with_resource);
    return 0;
  }

  result_size = snprintf(result_uri, uri_buffer_size, "ippusb://%s%s", hostname, resource);
  if (result_size < 0 || result_size >= uri_buffer_size) {
    _cupsLangPrintf(stderr, _("Failed to encode URI with new scheme and hostname"));
    return 0;
  }

  return 1;
}


/*
 * 'get_printer_ppd()' - Get an IPP Everywhere PPD file for the given URI.
 */

lpadmin_status_t				/* O  - Status */
get_printer_ppd(
    const char    *uri,			/* I  - Printer URI */
    char          *buffer,		/* I  - Filename buffer */
    size_t        bufsize,		/* I  - Size of filename buffer */
    int           *num_options,		/* IO - Number of options */
    cups_option_t **options)		/* IO - Options */
{
  http_t	*http;			/* Connection to printer */
  ipp_t		*request,		/* Get-Printer-Attributes request */
		*response;		/* Get-Printer-Attributes response */
  ipp_attribute_t *attr;		/* Attribute from response */
  char		resolved[1024],		/* Resolved URI */
		scheme[32],		/* URI scheme */
		userpass[256],		/* Username:password */
		host[256],		/* Hostname */
		resource[256];		/* Resource path */
  int		port;			/* Port number */

 /*
  * Connect to the printer...
  */

  if (strstr(uri, "._tcp"))
  {
   /*
    * Resolve URI...
    */

    if (!_httpResolveURI(uri, resolved, sizeof(resolved), _HTTP_RESOLVE_DEFAULT, NULL, NULL))
    {
      _cupsLangPrintf(stderr, _("%s: Unable to resolve \"%s\"."), "lpadmin", uri);
      return LPAS_PRINTER_UNREACHABLE;
    }

    uri = resolved;
  }

  if (httpSeparateURI(HTTP_URI_CODING_ALL, uri, scheme, sizeof(scheme), userpass, sizeof(userpass), host, sizeof(host), &port, resource, sizeof(resource)) < HTTP_URI_STATUS_OK)
  {
    _cupsLangPrintf(stderr, _("%s: Bad printer URI \"%s\"."), "lpadmin", uri);
    return LPAS_UNKNOWN_ERROR;
  }

  // If the scheme is ippusb then the host is replaced with a dedicated socket
  // in /run/ippusb.  We wait briefly to make sure the socket is available
  // before connecting.
  if (!strcmp(scheme, "ippusb")) {
    char* socket = ippusb_host_to_socket_name(host);
    if (socket == NULL)
      return status_from_last_error_code();
    _cupsLangPrintf(stderr, _("lpadmin: received socket name \"%s\""), socket);

    int ret = snprintf(host, sizeof(host), "/run/ippusb/%s", socket);
    free(socket);
    if (ret < 0 || ret >= sizeof(host)) {
      _cupsLangPrintf(stderr, _("lpadmin: Failed to overwrite host"));
      return LPAS_UNKNOWN_ERROR;
    }

    // Wait a maximum of 6 seconds for the socket to be created.
    if (wait_for_socket(host, 6) < 0)
      return status_from_last_error_code();
  }

  http = httpConnect2(host, port, NULL, AF_UNSPEC,
                      !strcmp(scheme, "ipps") ? HTTP_ENCRYPTION_ALWAYS
                                              : HTTP_ENCRYPTION_IF_REQUESTED,
                      1, 30000, NULL);
  if (!http) {
    _cupsLangPrintf(stderr, _("%s: Unable to connect to \"%s:%d\": %s"),
                    "lpadmin", host, port, cupsLastErrorString());
    return status_from_last_error_code();
  }

  // Allow printers up to 15 seconds to respond since some can be quite slow
  // to initialize.  Anecdotally, we see printers take up to 10 seconds to
  // start.
  httpSetTimeout(http, 15, NULL, NULL);

 /*
  * Send a Get-Printer-Attributes request...
  */

  // This will zero-fill the array with a size of |MAX_IPPUSB_URI|.
  char ippusb_uri[MAX_IPPUSB_URI] = {0};
  int use_ippusb = 0;
  if (!strcmp(scheme, "ippusb")) {
    // Change the uri back to use the ipp scheme for communicating with the cups
    // server and so that communications will be understood by the printer.
    // We can't simply change the existing uri because we want lpadmin to save
    // the printer in the system as "ippusb", but need the "ipp" scheme in
    // |ippusb_uri| in order to communicate with the printer.
    if (!change_scheme(uri, "ipp", HTTP_MAX_URI, ippusb_uri)) {
      _cupsLangPrintf(stderr, _("%s: Failed to change uri to %s"), "lpadmin",
                      "ipp");
      return LPAS_UNKNOWN_ERROR;
    }
    use_ippusb = 1;
  }

/* We do not use the command below because some printers cannot interpret this
   field correctly and skip important parameters in the response.
  ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD, "requested-attributes", sizeof(pattrs) / sizeof(pattrs[0]), NULL, pattrs);
 */

  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
               use_ippusb ? ippusb_uri : uri);

  // First we send a "Get-Printer-Attributes" request which simply requests the
  // supported document formats. This is done so that if the printer supports
  // one of either PDF or PWG-Raster as an input format we can request all of
  // the printer's supported attributes for that specific format.
  //
  // Document formats are prioritized in the following order:
  //
  // PDF > PWG-Raster > everything else
  ipp_t *document_formats_response =
      request_document_formats(http, resource, use_ippusb ? ippusb_uri : uri);
  int document_format = 0;
  if (document_formats_response == NULL) {
    _cupsLangPrintf(stderr,
                    _("%s: Failed to execute Get-Printer-Attributes request "
                      "for requested-attributes document-format-supported"),
                    "lpadmin");
  } else {
    ipp_attribute_t *document_formats = ippFindAttribute(
        document_formats_response, "document-format-supported", IPP_TAG_MIMETYPE);
    if (document_formats == NULL) {
      _cupsLangPrintf(stderr,
                      _("%s: Couldn't find 'document-format-supported' in "
                        "Get-Printer-Attributes response"),
                      "lpadmin");
    } else {
      // If |document_formats| contains one of our preferred input types, then
      // specify that type in our Get-Printer-Attributes request.
      if (ippContainsString(document_formats, "application/pdf")) {
        document_format = 1;
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE,
                                       "document-format", NULL, "application/pdf");
      } else if (ippContainsString(document_formats, "image/pwg-raster")) {
        document_format = 1;
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE,
                                       "document-format", NULL, "image/pwg-raster");
      } else {
        _cupsLangPrintf(stderr,
                        _("%s: Printer does not support either 'application/pdf' "
                          "or 'image/pwg-raster' formats."),
                        "lpadmin");
      }
    }
    ippDelete(document_formats_response);
  }

  response = cupsDoRequest(http, request, resource);
  if (response == NULL && document_format) {
    _cupsLangPrintf(stderr,
                    _("%s: Failed to execute Get-Printer-Attributes request"
		      " - retrying without document-format attribute..."),
		      "lpadmin");
    request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
                 use_ippusb ? ippusb_uri : uri);
    response = cupsDoRequest(http, request, resource);
  }
  if (cupsLastError() >= IPP_STATUS_REDIRECTION_OTHER_SITE)
  {
    _cupsLangPrintf(stderr, _("%s: Unable to query printer(%d): %s"), "lpadmin",
	 cupsLastError(), cupsLastErrorString());
    buffer[0] = '\0';
  }
  if (response == NULL) {
    _cupsLangPrintf(stderr,
                    _("%s:  Failed to execute Get-Printer-Attributes request"),
		      "lpadmin");
    return status_from_last_error_code();
  }
  if (!ippFindAttribute(response, "media-col-database", IPP_TAG_BEGIN_COLLECTION)) {
     ipp_t *request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
     ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
                    use_ippusb ? ippusb_uri : uri);
     ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                  "requested-attributes", NULL, "media-col-database");
     ipp_t *db = cupsDoRequest(http, request, resource);
     if (db) {
       ippCopyAttribute(response, ippFindAttribute(db, "media-col-database", IPP_TAG_BEGIN_COLLECTION), 0);
       ippDelete(db);
     }
  }

  // Try to ensure printer-strings-uri is present if possible, and in the closest possible
  // language to the user's.
  const char *closest_language = NULL;
  if ((attr = ippFindAttribute(response, "printer-strings-languages-supported", IPP_TAG_LANGUAGE)))
  {
    // find the closest language in printer-strings-languages-supported
    closest_language = get_closest_language(attr);
  }
  else if ((attr = ippFindAttribute(response, "generated-natural-language-supported", IPP_TAG_LANGUAGE)))
  {
    // if that attribute doesn't exist, find the closest language in generated-natural-language-supported
    closest_language = get_closest_language(attr);
  }

  // check whether natural-language-configured is already the closest language
  if (use_ippusb ||
      (closest_language && (attr = ippFindAttribute(response, "natural-language-configured", IPP_TAG_LANGUAGE)) &&
      _cups_strcasecmp(closest_language, ippGetString(attr, 0, NULL))))
  {
    // if the closest language isn't already the configured language, resend the request
    if (closest_language) {
      setenv("CROS_CUPS_LANGUAGE", closest_language, 1);
      _cupsLangPrintf(stderr, _("%s: re-requesting printer strings with language %s"), "lpadmin", closest_language);
    }
    ipp_t *request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
                  use_ippusb ? ippusb_uri : uri);
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                  "requested-attributes", NULL, "printer-strings-uri");
    ipp_t *strings_response = cupsDoRequest(http, request, resource);
    if (strings_response)
    {
      if ((attr = ippFindAttribute(strings_response, "printer-strings-uri", IPP_TAG_URI)))
      {
        // replace the old printer-strings-uri attribute with the new one
        _cupsLangPrintf(stderr, _("%s: request successful, replacing printer-strings-uri attribute"), "lpadmin");
        ippDeleteAttribute(response, ippFindAttribute(response, "printer-strings-uri", IPP_TAG_ZERO));
        ippCopyAttribute(response, attr, 0);
        if (use_ippusb) {
          char ippusb_strings_uri[1024];
          attr = ippFindAttribute(response, "printer-strings-uri", IPP_TAG_ZERO);
          if (url_to_ippusb_host(ippusb_uri, ippGetString(attr, 0, NULL), ippusb_strings_uri, sizeof(ippusb_strings_uri))) {
            ippSetString(response, &attr, 0, ippusb_strings_uri);
          }
        }
      }
      ippDelete(strings_response);
    }
  }

  if (_ppdCreateFromIPP(buffer, bufsize, response))
  {
    if (!cupsGetOption("printer-geo-location", *num_options, *options) && (attr = ippFindAttribute(response, "printer-geo-location", IPP_TAG_URI)) != NULL)
      *num_options = cupsAddOption("printer-geo-location", ippGetString(attr, 0, NULL), *num_options, options);

    if (!cupsGetOption("printer-info", *num_options, *options) && (attr = ippFindAttribute(response, "printer-info", IPP_TAG_TEXT)) != NULL)
      *num_options = cupsAddOption("printer-info", ippGetString(attr, 0, NULL), *num_options, options);

    if (!cupsGetOption("printer-location", *num_options, *options) && (attr = ippFindAttribute(response, "printer-location", IPP_TAG_TEXT)) != NULL)
      *num_options = cupsAddOption("printer-location", ippGetString(attr, 0, NULL), *num_options, options);
  }
  else
    _cupsLangPrintf(stderr, _("%s: Unable to create PPD file: %s"), "lpadmin", strerror(errno));

  ippDelete(response);
  httpClose(http);

  if (buffer[0])
    return LPAS_OK;

  if (ec_last_error() == EC_IPP_ATTRIBUTE)
    return LPAS_PRINTER_NOT_AUTOCONFIGURABLE;

  return status_from_last_error_code();
}


/*
 * 'get_printer_type()' - Determine the printer type and URI.
 */

static cups_ptype_t			/* O - printer-type value */
get_printer_type(http_t *http,		/* I - Server connection */
                 char   *printer,	/* I - Printer name */
		 char   *uri,		/* I - URI buffer */
                 size_t urisize)	/* I - Size of URI buffer */
{
  ipp_t			*request,	/* IPP request */
			*response;	/* IPP response */
  ipp_attribute_t	*attr;		/* printer-type attribute */
  cups_ptype_t		type;		/* printer-type value */


 /*
  * Build a GET_PRINTER_ATTRIBUTES request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  *    requested-attributes
  *    requesting-user-name
  */

  httpAssembleURIf(HTTP_URI_CODING_ALL, uri, (int)urisize, "ipp", NULL, "localhost", ippPort(), "/printers/%s", printer);

  request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
               "printer-uri", NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
               "requested-attributes", NULL, "printer-type");
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
               "requesting-user-name", NULL, cupsUser());

 /*
  * Do the request...
  */

  response = cupsDoRequest(http, request, "/");
  if ((attr = ippFindAttribute(response, "printer-type",
                               IPP_TAG_ENUM)) != NULL)
  {
    type = (cups_ptype_t)attr->values[0].integer;

    if (type & CUPS_PRINTER_CLASS)
      httpAssembleURIf(HTTP_URI_CODING_ALL, uri, (int)urisize, "ipp", NULL, "localhost", ippPort(), "/classes/%s", printer);
  }
  else
    type = CUPS_PRINTER_LOCAL;

  ippDelete(response);

  return (type);
}

/*
 * 'create_temp_file_with_content()' - Creates temporary file and write to it
 *   whole content from given file descriptor. No seek operations are
 *   performed on the input file, it is read to its end. Created temporary file
 *   is seek to the beginning before return. The function returns a file
 *   descriptor to the temporary file or value <0 in case of an error.
 */
static int create_temp_file_with_content(
    int  fd_content)	/* a file with content to copy */
{
  FILE * ftmpfile;
  int fd_tmp;
  unsigned const buf_size = 4096;
  uint8_t buf[buf_size];
  size_t bytes_read;
  size_t bytes_written;
  size_t bytes_count;

  if (fd_content < 0) {
    return -1;
  }

  ftmpfile = tmpfile();
  if (ftmpfile == NULL) {
    return -2;
  }
  fd_tmp = fileno(ftmpfile);
  if (fd_tmp < 0) {
    return -3;
  }

  while (1) {
    bytes_read = (size_t)read(fd_content, buf, buf_size);
    if (bytes_read < 0) {
      close(fd_tmp);
      return -4;
    }
    if (bytes_read == 0) {
      break;
    }
    bytes_written = 0;
    while (bytes_read > bytes_written) {
      bytes_count = (size_t)write(fd_tmp, buf + bytes_written,
                                  bytes_read - bytes_written);
      if (bytes_count < 0) {
          close(fd_tmp);
          return -5;
      }
      bytes_written += bytes_count;
    }
  }

  if (lseek(fd_tmp, 0, SEEK_SET) < 0) {
    close(fd_tmp);
    return -6;
  }

  return fd_tmp;
}


/*
 * 'set_printer_options()' - Set the printer options.
 */

static int				/* O - 0 on success, 1 on fail */
set_printer_options(
    http_t        *http,		/* I - Server connection */
    char          *printer,		/* I - Printer */
    int           num_options,		/* I - Number of options */
    cups_option_t *options,		/* I - Options */
    char          *file,		/* I - PPD file */
    int           enable)		/* I - Enable printer? */
{
  ipp_t		*request;		/* IPP Request */
  const char	*ppdfile;		/* PPD filename */
  ppd_file_t	*ppd;			/* PPD file */
  ppd_choice_t	*choice;		/* Marked choice */
  char		uri[HTTP_MAX_URI],	/* URI for printer/class */
		line[1024],		/* Line from PPD file */
		keyword[1024],		/* Keyword from Default line */
		*keyptr,		/* Pointer into keyword... */
		tempfile[1024];		/* Temporary filename */
  cups_file_t	*in,			/* PPD file */
		*out;			/* Temporary file */
  const char	*ppdname,		/* ppd-name value */
		*protocol,		/* Old protocol option */
		*customval,		/* Custom option value */
		*boolval;		/* Boolean value */
  int		wrote_ipp_supplies = 0,	/* Wrote cupsIPPSupplies keyword? */
		wrote_snmp_supplies = 0,/* Wrote cupsSNMPSupplies keyword? */
		copied_options = 0;	/* Copied options? */
  int fd_ppd_ppd;	/* Descriptor to file with PPD content for ppd_file_t  */
  int fd_ppd_cups;	/* Descriptor to file with PPD content for cups_file_t */


 /*
  * Build a CUPS-Add-Modify-Printer or CUPS-Add-Modify-Class request,
  * which requires the following attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  *    requesting-user-name
  *    other options
  */

  if (get_printer_type(http, printer, uri, sizeof(uri)) & CUPS_PRINTER_CLASS)
    request = ippNewRequest(IPP_OP_CUPS_ADD_MODIFY_CLASS);
  else
    request = ippNewRequest(IPP_OP_CUPS_ADD_MODIFY_PRINTER);

  _cupsLangPrintf(stderr, _("lpadmin: printer uri %s"), uri);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL, uri);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name", NULL, cupsUser());

 /*
  * Add the options...
  */

  if (file)
    ppdfile = file;
  else if ((ppdname = cupsGetOption("ppd-name", num_options, options)) != NULL && strcmp(ppdname, "everywhere") && strcmp(ppdname, "raw") && num_options > 1)
  {
    if ((ppdfile = cupsGetServerPPD(http, ppdname)) != NULL)
    {
     /*
      * Copy options array and remove ppd-name from it...
      */

      cups_option_t *temp = NULL, *optr;
      int i, num_temp = 0;
      for (i = num_options, optr = options; i > 0; i --, optr ++)
        if (strcmp(optr->name, "ppd-name"))
          num_temp = cupsAddOption(optr->name, optr->value, num_temp, &temp);

      copied_options = 1;
      num_options    = num_temp;
      options        = temp;
    }
  }
  else if (request->request.op.operation_id == IPP_OP_CUPS_ADD_MODIFY_PRINTER)
    ppdfile = cupsGetPPD(printer);
  else
    ppdfile = NULL;

  cupsEncodeOptions2(request, num_options, options, IPP_TAG_OPERATION);

  if (enable)
  {
    ippAddInteger(request, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-state", IPP_PSTATE_IDLE);
    ippAddBoolean(request, IPP_TAG_PRINTER, "printer-is-accepting-jobs", 1);
  }

  cupsEncodeOptions2(request, num_options, options, IPP_TAG_PRINTER);

  if ((protocol = cupsGetOption("protocol", num_options, options)) != NULL)
  {
    if (!_cups_strcasecmp(protocol, "bcp"))
      ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_NAME, "port-monitor",
                   NULL, "bcp");
    else if (!_cups_strcasecmp(protocol, "tbcp"))
      ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_NAME, "port-monitor",
                   NULL, "tbcp");
  }

  if (ppdfile)
  {

   /*
    * Open tempfile
    */
    if ((out = cupsTempFile2(tempfile, sizeof(tempfile))) == NULL)
    {
      _cupsLangPrintError(NULL, _("lpadmin: Unable to create temporary file"));
      goto error;
    }

   /*
    * Open PPD file (twice)
    */
    if (ppdfile[0] == '-' && ppdfile[1] == '\0') {
      /* The content given on the input must be read twice from file
       * descriptors. Moreover, the descriptors are closed at the end. To make
       * it possible we have to create two temporary files with the copy of the
       * content sent via standard input.
       */
      fd_ppd_ppd  = create_temp_file_with_content(STDIN_FILENO);
      fd_ppd_cups = create_temp_file_with_content(fd_ppd_ppd);
      if (fd_ppd_ppd < 0 || fd_ppd_cups < 0) {
        _cupsLangPrintf(stderr, _("lpadmin: Cannot create temporary files."));
        unlink(tempfile);
        goto error;
      }
      if (lseek(fd_ppd_ppd, 0, SEEK_SET) < 0) {
        _cupsLangPrintf(stderr, _("lpadmin: Temporary file: lseek failed"));
        unlink(tempfile);
        goto error;
      }
      ppd = ppdOpenFd(fd_ppd_ppd);
      in = cupsFileOpenFd(fd_ppd_cups, "r");
    } else {
      /* The content is given as standard file, we just open it twice. */
      ppd = ppdOpenFile(ppdfile);
      in = cupsFileOpen(ppdfile, "r");
    }

    if (ppd == NULL)
    {
      int		linenum;	/* Line number of error */
      ppd_status_t	status = ppdLastError(&linenum); 	/* Status code */
      _cupsLangPrintf(stderr, _("lpadmin: Unable to open PPD \"%s\": %s on line %d."), ppdfile, ppdErrorString(status), linenum);
      unlink(tempfile);
      goto error;
    }

    if (in == NULL)
    {
      _cupsLangPrintf(stderr, _("lpadmin: Unable to open PPD \"%s\": %s"), ppdfile, strerror(errno));
      cupsFileClose(out);
      unlink(tempfile);
      goto error;
    }

    /*
     * Set default options in the PPD file...
     */
    ppdMarkDefaults(ppd);
    cupsMarkOptions(ppd, num_options, options);


    while (cupsFileGets(in, line, sizeof(line)))
    {
      if (!strncmp(line, "*cupsIPPSupplies:", 17) &&
	  (boolval = cupsGetOption("cupsIPPSupplies", num_options,
	                           options)) != NULL)
      {
        wrote_ipp_supplies = 1;
        cupsFilePrintf(out, "*cupsIPPSupplies: %s\n",
	               (!_cups_strcasecmp(boolval, "true") ||
		        !_cups_strcasecmp(boolval, "yes") ||
		        !_cups_strcasecmp(boolval, "on")) ? "True" : "False");
      }
      else if (!strncmp(line, "*cupsSNMPSupplies:", 18) &&
	       (boolval = cupsGetOption("cupsSNMPSupplies", num_options,
	                                options)) != NULL)
      {
        wrote_snmp_supplies = 1;
        cupsFilePrintf(out, "*cupsSNMPSupplies: %s\n",
	               (!_cups_strcasecmp(boolval, "true") ||
		        !_cups_strcasecmp(boolval, "yes") ||
		        !_cups_strcasecmp(boolval, "on")) ? "True" : "False");
      }
      else if (strncmp(line, "*Default", 8))
        cupsFilePrintf(out, "%s\n", line);
      else
      {
       /*
        * Get default option name...
	*/

        strlcpy(keyword, line + 8, sizeof(keyword));

	for (keyptr = keyword; *keyptr; keyptr ++)
	  if (*keyptr == ':' || isspace(*keyptr & 255))
	    break;

        *keyptr++ = '\0';
        while (isspace(*keyptr & 255))
	  keyptr ++;

        if (!strcmp(keyword, "PageRegion") ||
	    !strcmp(keyword, "PageSize") ||
	    !strcmp(keyword, "PaperDimension") ||
	    !strcmp(keyword, "ImageableArea"))
	{
	  if ((choice = ppdFindMarkedChoice(ppd, "PageSize")) == NULL)
	    choice = ppdFindMarkedChoice(ppd, "PageRegion");
        }
	else
	  choice = ppdFindMarkedChoice(ppd, keyword);

        if (choice && strcmp(choice->choice, keyptr))
	{
	  if (strcmp(choice->choice, "Custom"))
	  {
	    cupsFilePrintf(out, "*Default%s: %s\n", keyword, choice->choice);
	  }
	  else if ((customval = cupsGetOption(keyword, num_options,
	                                      options)) != NULL)
	  {
	    cupsFilePrintf(out, "*Default%s: %s\n", keyword, customval);
	  }
	  else
	    cupsFilePrintf(out, "%s\n", line);
	}
	else
	  cupsFilePrintf(out, "%s\n", line);
      }
    }

    if (!wrote_ipp_supplies &&
	(boolval = cupsGetOption("cupsIPPSupplies", num_options,
				 options)) != NULL)
    {
      cupsFilePrintf(out, "*cupsIPPSupplies: %s\n",
		     (!_cups_strcasecmp(boolval, "true") ||
		      !_cups_strcasecmp(boolval, "yes") ||
		      !_cups_strcasecmp(boolval, "on")) ? "True" : "False");
    }

    if (!wrote_snmp_supplies &&
        (boolval = cupsGetOption("cupsSNMPSupplies", num_options,
			         options)) != NULL)
    {
      cupsFilePrintf(out, "*cupsSNMPSupplies: %s\n",
		     (!_cups_strcasecmp(boolval, "true") ||
		      !_cups_strcasecmp(boolval, "yes") ||
		      !_cups_strcasecmp(boolval, "on")) ? "True" : "False");
    }

    cupsFileClose(in);
    cupsFileClose(out);
    ppdClose(ppd);

   /*
    * Do the request...
    */

    ippDelete(cupsDoFileRequest(http, request, "/admin/", tempfile));

   /*
    * Clean up temp files... (TODO: catch signals in case we CTRL-C during
    * lpadmin)
    */

    if (ppdfile != file)
      unlink(ppdfile);
    unlink(tempfile);
  }
  else
  {
   /*
    * No PPD file - just set the options...
    */

    ippDelete(cupsDoRequest(http, request, "/admin/"));
  }

  if (copied_options)
    cupsFreeOptions(num_options, options);

 /*
  * Check the response...
  */

  if (cupsLastError() > IPP_STATUS_OK_CONFLICTING)
  {
    _cupsLangPrintf(stderr, _("%s: %s"), "lpadmin", cupsLastErrorString());

    return (1);
  }
  else
    return (0);

 /*
  * Error handling...
  */

  error:

  ippDelete(request);

  if (ppdfile != file)
    unlink(ppdfile);

  if (copied_options)
    cupsFreeOptions(num_options, options);

  return (1);
}


/*
 * 'usage()' - Show program usage and exit.
 */

static void
usage(void)
{
  _cupsLangPuts(stdout, _("Usage: lpadmin [options] -d destination\n"
                          "       lpadmin [options] -p destination\n"
                          "       lpadmin [options] -p destination -c class\n"
                          "       lpadmin [options] -p destination -r class\n"
                          "       lpadmin [options] -x destination"));
  _cupsLangPuts(stdout, _("Options:"));
  _cupsLangPuts(stdout, _("-c class                Add the named destination to a class"));
  _cupsLangPuts(stdout, _("-d destination          Set the named destination as the server default"));
  _cupsLangPuts(stdout, _("-D description          Specify the textual description of the printer"));
  _cupsLangPuts(stdout, _("-E                      Encrypt the connection to the server"));
  _cupsLangPuts(stdout, _("-E                      Enable and accept jobs on the printer (after -p)"));
  _cupsLangPuts(stdout, _("-h server[:port]        Connect to the named server and port"));
  _cupsLangPuts(stdout, _("-i ppd-file             Specify a PPD file for the printer"));
  _cupsLangPuts(stdout, _("-L location             Specify the textual location of the printer"));
  _cupsLangPuts(stdout, _("-m model                Specify a standard model/PPD file for the printer"));
  _cupsLangPuts(stdout, _("-m everywhere           Specify the printer is compatible with IPP Everywhere"));
  _cupsLangPuts(stdout, _("-o name-default=value   Specify the default value for the named option"));
  _cupsLangPuts(stdout, _("-o Name=Value           Specify the default value for the named PPD option "));
  _cupsLangPuts(stdout, _("-o cupsIPPSupplies=false\n"
                          "                        Disable supply level reporting via IPP"));
  _cupsLangPuts(stdout, _("-o cupsSNMPSupplies=false\n"
                          "                        Disable supply level reporting via SNMP"));
  _cupsLangPuts(stdout, _("-o job-k-limit=N        Specify the kilobyte limit for per-user quotas"));
  _cupsLangPuts(stdout, _("-o job-page-limit=N     Specify the page limit for per-user quotas"));
  _cupsLangPuts(stdout, _("-o job-quota-period=N   Specify the per-user quota period in seconds"));
  _cupsLangPuts(stdout, _("-o printer-error-policy=name\n"
                          "                        Specify the printer error policy"));
  _cupsLangPuts(stdout, _("-o printer-is-shared=true\n"
                          "                        Share the printer"));
  _cupsLangPuts(stdout, _("-o printer-op-policy=name\n"
                          "                        Specify the printer operation policy"));
  _cupsLangPuts(stdout, _("-p destination          Specify/add the named destination"));
  _cupsLangPuts(stdout, _("-r class                Remove the named destination from a class"));
  _cupsLangPuts(stdout, _("-R name-default         Remove the default value for the named option"));
  _cupsLangPuts(stdout, _("-u allow:all            Allow all users to print"));
  _cupsLangPuts(stdout, _("-u allow:list           Allow the list of users or groups (@name) to print"));
  _cupsLangPuts(stdout, _("-u deny:list            Prevent the list of users or groups (@name) to print"));
  _cupsLangPuts(stdout, _("-U username             Specify the username to use for authentication"));
  _cupsLangPuts(stdout, _("-v device-uri           Specify the device URI for the printer"));
  _cupsLangPuts(stdout, _("-x destination          Remove the named destination"));

  exit(LPAS_WRONG_PARAMETERS);
}


/*
 * 'validate_name()' - Make sure the printer name only contains valid chars.
 */

static int				/* O - 0 if name is no good, 1 if name is good */
validate_name(const char *name)		/* I - Name to check */
{
  const char	*ptr;			/* Pointer into name */


 /*
  * Scan the whole name...
  */

  for (ptr = name; *ptr; ptr ++)
    if (*ptr == '@')
      break;
    else if ((*ptr >= 0 && *ptr <= ' ') || *ptr == 127 || *ptr == '/' || *ptr == '\\' || *ptr == '?' || *ptr == '\'' || *ptr == '\"' || *ptr == '#')
      return (0);

 /*
  * All the characters are good; validate the length, too...
  */

  return ((ptr - name) < 128);
}

// Sends a Get-Printer-Attributes request for specifically the
// "document-format-supported" attribute to the printer connection defined by
// the given |http| and |resource|.
static ipp_t *request_document_formats(http_t *http, const char *resource,
                                       const char *uri) {
  ipp_t *request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri", NULL,
               uri);
  const char *const pattrs[] = {"document-format-supported"};
  ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
                "requested-attributes", sizeof(pattrs) / sizeof(pattrs[0]),
                NULL, pattrs);
  return cupsDoRequest(http, request, resource);
}


static lpadmin_status_t status_from_last_error_code()
{
  switch (ec_last_error()) {
  case EC_IO:
    return LPAS_IO_ERROR;
  case EC_MEMORY:
    return LPAS_MEMORY_ALLOC_ERROR;
  case EC_DESTINATION_UNREACHABLE:
    return LPAS_PRINTER_UNREACHABLE;
  case EC_UNEXPECTED_RESPONSE:
    return LPAS_PRINTER_WRONG_RESPONSE;
  default:
    /* to avoid compiler's warning */
    break;
  }
  return LPAS_UNKNOWN_ERROR;
}
