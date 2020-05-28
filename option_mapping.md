# IPP Attribute to PPD option mapping
In order to support features that differ from PPD to PPD, the ppd-cache version
of a printer now includes a series of mappings that are used to determine what
options are passed to the filters. What _ppdConvertOptions expects to find is as
follows:

## option-mappings
The (cupsd_printer_t*)p->ppd_attrs should contain an ipp collection called
"option-mappings". It is expect that this collection will contain subcollections
which indicate how IPP attributes found in a print job are to be converted into
filter options. The name of these subcollections should be the name of the
IPP attribute that should be translated:

`
ipp_t *option_mappings = ippFindAttribute(p->ppd_attrs,
                                          "option-mappings",
                                          IPP_TAG_BEGIN_COLLECTION);
ipp_t *ipp_attr = ippNew();
ippAddString(ipp_attr, IPP_TAG_ZERO, IPP_TAG_TEXT, "setting", NULL, "True");
ippAddString(ipp_attr, IPP_TAG_ZERO, IPP_TAG_TEXT, "foo", NULL, "%s");
ippAddString(ipp_attr, IPP_TAG_ZERO, IPP_TAG_TEXT, "bar", NULL, "Custom.%s");
ippAddCollection(option_mappings, IPP_TAG_ZERO, "ipp-attr", ipp_attr);
`

If a job comes in whose job->attrs contains an "ipp-attr" whose value is
baz@Domain.com, then this mapping will result in the following three options
being passed to the filter:

`
setting=True
foo=baz@Domain.com
bar=Custom.baz@Domain.com
`

The only substitutions allowed currently are "%s" for the transformed value of
the attribute. Non-string values will be converted to strings.

### transformations
Sometimes IPP values cannot be converted directly into PPD values, so
transformations can also be added to the collection. The name of a
transformation must begin with "_", and the transformation should be in the same collection which contains the mappings for the specified ippAttribute:

`
ipp_t *option_mappings = ippFindAttribute(p->ppd_attrs,
                                          "option-mappings",
                                          IPP_TAG_BEGIN_COLLECTION);
ipp_t *ipp_attr = ippNew();
ippAddString(ipp_attr, IPP_TAG_ZERO, IPP_TAG_TEXT, "bar", NULL, "Custom.%s");
ippAddString(ipp_attr, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "_delimiters", NULL, "@");
ippAddCollection(option_mappings, IPP_TAG_ZERO, "ipp-attr", ipp_attr);
`

Transformations are applied to the ipp value before it is added to the option
strings. And they are applied in the order they are found in the collection.
So in this case, if a job comes in whose job_attrs contains the
"ipp-attr=baz@Domain.com", then the following options would be output:

`
bar=Custom.baz
`

The currently available transformations are:

#### _delimiters
Delimiters must always be a IPP_TAG_KEYWORD, and the string is the list of
characters that terminate the value. So for example:

`
ippAddString(ipp_attr, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "_delimiters", NULL, "@");
`

Means that only characters before the first '@' in a ipp attribute value will
be passed as an option value.

#### _valid-chars
Valid Chars must always be an IPP_TAG_KEYWORD, and the string is the list of
characters that are allowed in as an option value. Any invalid character is
replace with the character found in the _replacement-char transformation. So if
the following transformations were in place:

`
ippAddString(ipp_attr, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "_valid-chars", NULL,
             "abcdefghijklmnopqrstuvwxyz");
ippAddString(ipp_attr, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "_replacement-char", NULL,
             "-");
`

And the value of the specified ipp attribute was "ipp-attr=baz@Domain.com", the transformed value used for the options would be:

`
baz--omain-com
`
#### _constraint
Constraint must always be an IPP_TAG_RANGE attribute, and it contains the
minimum and maximum allowed characters for a filter option. If the value is
shorter than the minimum, then the mapping is aborted and no options are passed
to the filter for this transformation. If the value is longer than the maximum,
then the string is truncated to that length. So if this transformation was in
place:

`
ippAddRange(ipp_attr, IPP_TAG_ZERO, "_constraint", 2, 8);
`

And the value of the specified ipp attribute was "ipp-attr=baz@Domain.com", the transformed value used for the options would be:

`
baz@Doma
`

#### _mapping
Mapping is always an ippCollection that consists of a series of IPP_TAG_KEYWORDs
whose name is the IPP value and whose value is the PPD filter option value. If
the ipp value can be found in the collection, then it is exchanged for the value
of that keyword. If not, then the ipp attribute is discard and not converted
into filter options. So if the following keywords existed in the mapping:

`
ipp_t *mapping = ippNew();
ippAddString(mapping, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "baz", NULL, "bottle");
ippAddString(mapping, IPP_TAG_ZERO, IPP_TAG_KEYWORD, "bat", NULL, "ball");
ippAddCollection(ipp_attr, IPP_TAG_ZERO, "_mapping", mapping);
`

And the value of the specified ipp attribute was "ipp-attr=baz", the transformed
value used for the options would be:

`
bottle
`



