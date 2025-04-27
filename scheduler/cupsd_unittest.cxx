// Copyright 2020 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include "cupsd.h"
}

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <cups/ipp.h>

#include "base/files/scoped_temp_dir.h"
#include "base/files/file_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "brillo/file_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

constexpr char kPrinter[] = "thefake";
const base::FilePath kPpdPath =
    base::FilePath("conf/ppd").Append(kPrinter).AddExtension("ppd");

struct ClientInfo {
  std::string name;
  std::string str_version;
  std::optional<std::string> version;
  std::optional<std::string> patches;
  int type;
};

class PrintJob : public testing::Test {
 public:
  PrintJob() : job_(cupsdAddJob(0, "")) {}

  ~PrintJob() {
    if (job_) {
      EXPECT_TRUE(job_->attrs);
      if (job_->attrs) {
        ippDelete(job_->attrs);
        job_->attrs = nullptr;
      }
      if (job_->printer) {
        cupsdDeletePrinter(job_->printer, true);
        job_->printer = nullptr;
      }
      EXPECT_FALSE(base::PathExists(kPpdPath));
      cupsdDeleteJob(job_, CUPSD_JOB_PURGE);
    }
  }

  PrintJob(const PrintJob&) = delete;
  PrintJob& operator=(const PrintJob&) = delete;

  void SetUp() {
    ASSERT_TRUE(job_);
    ASSERT_FALSE(job_->attrs);
    ASSERT_FALSE(job_->printer);
    ASSERT_FALSE(base::PathExists(kPpdPath));
    job_->attrs = ippNew();
    ASSERT_TRUE(job_->attrs);
  }

  // Use the PPD string ppd_data to set printer capabilities.
  void SetPrinter(const std::string& ppd_data) const {
    if (job_->printer)
      cupsdDeletePrinter(job_->printer, true);
    job_->printer = cupsdAddPrinter(kPrinter);
    ASSERT_TRUE(job_->printer);
    job_->printer->temporary = true;
    EXPECT_FALSE(base::PathExists(kPpdPath));
    ASSERT_TRUE(brillo::WriteStringToFile(kPpdPath, ppd_data));
    cupsdSetPrinterAttrs(job_->printer);
  }

  // Chainable function to add a resolution to the IPP printer-resolution
  // attribute. If the y resolution is omitted, then it is set to res_x.
  // Resolutions are in dots per inch (DPI).
  const PrintJob& Resolution(int res_x, int res_y = -1) const {
    if (res_y == -1)
      res_y = res_x;
    ResolutionImpl(res_x, res_y);
    return *this;
  }

  // Chainable function to add the IPP job-password attribute.
  const PrintJob& Password(const std::string& password) const {
    AddIppStrings("job-password", {password});
    return *this;
  }

  // Chainable function to add the IPP print-quality attribute.
  const PrintJob& PrintQuality(const std::string& value) const {
    AddIppStrings("print-quality", {value});
    return *this;
  }

  // Chainable function to add the IPP finishings attribute.
  const PrintJob& Finishings(const std::string& value) const {
    AddIppStrings("finishings", {value});
    return *this;
  }

  // Chainable function to add the IPP document-format-supported attribute.
  const PrintJob& DocumentFormats(
      const std::vector<std::string>& values) const {
    AddIppStrings("document-format-supported", values, IPP_TAG_MIMETYPE);
    return *this;
  }

  // Chainable function to add the IPP print-quality-supported attribute.
  const PrintJob& PrintQualities(const std::vector<int>& values) const {
    AddIppEnums("print-quality-supported", values);
    return *this;
  }

  // Chainable function to add the IPP output-bin-supported attribute.
  const PrintJob& OutputBins(const std::vector<std::string>& values) const {
    AddIppStrings("output-bin-supported", values);
    return *this;
  }

  // Chainable function to add the IPP media-source-supported attribute.
  const PrintJob& MediaSources(const std::vector<std::string>& values) const {
    AddIppStrings("media-source-supported", values);
    return *this;
  }

  // Chainable function to add the IPP client-info attribute.
  const PrintJob& ClientInfos(
      const std::vector<ClientInfo>& client_infos) const {
    std::vector<ipp_t*> collections;
    for (const ClientInfo& client_info : client_infos) {
      ipp_t* ipp = ippNew();
      ippAddInteger(ipp, IPP_TAG_ZERO, IPP_TAG_ENUM, "client-type",
                    client_info.type);
      ippAddString(ipp, IPP_TAG_ZERO, IPP_TAG_NAME, "client-name", nullptr,
                   client_info.name.c_str());
      ippAddString(ipp, IPP_TAG_ZERO, IPP_TAG_TEXT, "client-string-version",
                   nullptr, client_info.str_version.c_str());
      if (client_info.patches.has_value()) {
        ippAddString(ipp, IPP_TAG_ZERO, IPP_TAG_TEXT, "client-patches", nullptr,
                     client_info.patches.value().c_str());
      }
      if (client_info.version.has_value()) {
        ippAddOctetString(ipp, IPP_TAG_ZERO, "client-version",
                          client_info.version.value().data(),
                          client_info.version.value().size());
      }
      collections.push_back(ipp);
    }
    AddIppCollections(
        "client-info",
        std::vector<const ipp_t*>(collections.begin(), collections.end()),
        IPP_TAG_OPERATION);

    for (ipp_t* collection : collections) {
      ippDelete(collection);
    }
    return *this;
  }

  // Check if the printer supports an IPP option with a given name and value.
  const bool CheckOptionSupported(const std::string& name,
                                  const std::string& value) const {
    EXPECT_TRUE(job_->printer);
    if (!job_->printer)
      return false;
    http_t http{};
    cups_dest_t dest{};
    cups_dinfo_t dinfo{};
    dinfo.attrs = job_->printer->ppd_attrs;
    return cupsCheckDestSupported(&http, &dest, &dinfo, name.c_str(),
                                  value.c_str());
  }

  // Return the value of the printer's IPP printer-resolution-default attribute
  // or an empty string if it doesn't exist.
  //
  // Note that printer-resolution-default is generated from the PPD data
  // provided to SetPrinter().
  std::string DefaultResolution() const {
    std::string ret;
    DefaultResolutionImpl(ret);
    return ret;
  }

  // Return the value of the printer's IPP print-quality-default attribute, or
  // IPP_QUALITY_NORMAL if it doesn't exist.
  int DefaultPrintQuality() const {
    int ret = IPP_QUALITY_NORMAL;
    DefaultPrintQualityImpl(ret);
    return ret;
  }

  // Return the value of the printer's IPP finishings-default attribute, or
  // IPP_FINISHINGS_NONE if it doesn't exist.
  int DefaultFinishings() const {
    int ret = IPP_FINISHINGS_NONE;
    DefaultFinishingsImpl(ret);
    return ret;
  }

  // Convert the IPP job attributes to print filter command-line arguments.
  // Clear the job attributes before returning the string of options.
  std::string Filter() const {
    std::string ret;
    FilterImpl(ret);
    if (job_->attrs)
      ippDelete(job_->attrs);
    job_->attrs = ippNew();
    EXPECT_TRUE(job_->attrs);
    return ret;
  }

  // Create a PPD file out of the IPP job attributes.
  // Clear the job attributes before returning the PPD file contents.
  std::string Ppd() const {
    std::string ret;
    PpdImpl(ret);
    if (job_->attrs)
      ippDelete(job_->attrs);
    job_->attrs = ippNew();
    EXPECT_TRUE(job_->attrs);
    return ret;
  }

 private:
  // We need separate void implementations of functions so that the
  // ASSERT macro can be used.
  void DefaultResolutionImpl(std::string& ret) const {
    ASSERT_TRUE(job_->printer);
    ipp_attribute_t* attr =
        ippFindAttribute(job_->printer->ppd_attrs, "printer-resolution-default",
                         IPP_TAG_RESOLUTION);
    if (!attr)
      return;
    ASSERT_EQ(1, attr->num_values);
    char* res = _resolutionToString(attr->values[0]);
    ASSERT_TRUE(res);
    ret = res;
    free(res);
  }

  void DefaultPrintQualityImpl(int& ret) const {
    ASSERT_TRUE(job_->printer);
    ipp_attribute_t* attr = ippFindAttribute(
        job_->printer->ppd_attrs, "print-quality-default", IPP_TAG_ENUM);
    if (!attr)
      return;
    ASSERT_EQ(1, attr->num_values);
    ret = ippGetInteger(attr, 0);
  }

  void DefaultFinishingsImpl(int& ret) const {
    ASSERT_TRUE(job_->printer);
    ipp_attribute_t* attr = ippFindAttribute(
        job_->printer->ppd_attrs, "finishings-default", IPP_TAG_ENUM);
    if (!attr)
      return;
    ASSERT_EQ(1, attr->num_values);
    ret = ippGetInteger(attr, 0);
  }

  void ResolutionImpl(int res_x, int res_y) const {
    ASSERT_TRUE(job_->attrs);
    ASSERT_TRUE(ippAddResolution(job_->attrs, IPP_TAG_JOB, "printer-resolution",
                                 IPP_RES_PER_INCH, res_x, res_y));
  }

  void FilterImpl(std::string& out) const {
    ASSERT_TRUE(job_->printer);
    ASSERT_TRUE(job_->attrs);
    // The copies and title buffers must have a size of at least 1 for
    // _cups_strlcpy() to work properly.
    char buf[] = {0};
    char* args = get_options(job_, false, buf, sizeof(buf), buf, sizeof(buf));
    ASSERT_TRUE(args);
    out = args;
    free(args);
    if (out.size() && out[0] == ' ')
      out = out.substr(1);
  }

  void PpdImpl(std::string& ret) const {
    ASSERT_TRUE(job_->attrs);
    // Create an A4 media-supported IPP attribute if a media attribute doesn't
    // already exist. This is required for PPD generation to succeed.
    if (!ippFindAttribute(job_->attrs, "media-size-supported",
                          IPP_TAG_BEGIN_COLLECTION) &&
        !ippFindAttribute(job_->attrs, "media-supported", IPP_TAG_ZERO))
      AddIppStrings("media-supported", {CUPS_MEDIA_A4});
    // Create a document-format-supported IPP attribute with value
    // application/pdf if an attribute doesn't already exist.
    // This is required for PPD generation to succeed.
    if (!ippFindAttribute(job_->attrs, "document-format-supported",
                          IPP_TAG_MIMETYPE))
      DocumentFormats({"application/pdf"});
    char file[1024];
    ASSERT_TRUE(_ppdCreateFromIPP(file, sizeof(file), job_->attrs));
    ASSERT_TRUE(base::ReadFileToString(base::FilePath(file), &ret));
    ASSERT_TRUE(!unlink(file));
  }

  void AddIppEnums(const std::string& name,
                   const std::vector<int>& values) const {
    ASSERT_TRUE(job_->attrs);
    ASSERT_TRUE(ippAddIntegers(job_->attrs, IPP_TAG_JOB, IPP_TAG_ENUM,
                               name.c_str(), values.size(), values.data()));
  }

  void AddIppStrings(const std::string& name,
                     const std::vector<std::string>& values,
                     ipp_tag_e value_tag = IPP_TAG_KEYWORD) const {
    ASSERT_TRUE(job_->attrs);
    std::vector<const char*> vals;
    for (const auto& value : values)
      vals.push_back(value.c_str());
    ASSERT_TRUE(ippAddStrings(job_->attrs, IPP_TAG_JOB, value_tag, name.c_str(),
                              vals.size(), nullptr, vals.data()));
  }

  void AddIppCollections(const std::string& name,
                         std::vector<const ipp_t*> collections,
                         ipp_tag_t group_tag) const {
    ASSERT_TRUE(job_->attrs);
    ASSERT_TRUE(ippAddCollections(job_->attrs, group_tag, name.c_str(),
                                  collections.size(), collections.data()));
  }

  cupsd_job_t* const job_;
};

// Helper function to generate a PPD from a PPD resolution attribute name,
// a list of resolution values, and optionally a default resolution value.
// By default the list of resolution values is empty.
std::string GeneratePpd(const std::string& res_name,
                        const std::vector<std::string>& res_values = {},
                        const std::string& res_default = "") {
  std::string ret = "*PPD-Adobe: 4.3\n";
  ret += "*OpenUI *" + res_name + "/" + res_name + ": PickOne\n";
  for (const std::string& res : res_values)
    ret += "*" + res_name + " " + res + "/NUL: \" \"\n";
  if (res_default.size())
    ret += "*Default" + res_name + ": " + res_default + "\n";
  ret += "*CloseUI: *" + res_name + "\n";
  return ret;
}

// Helper function to generate the expected resolution options string,
// which includes both the PPD and IPP resolution tags.
// res_value should not include the dpi suffix.
std::string ResOpt(const std::string& res_name, std::string res_value) {
  const std::string kIppRes = "printer-resolution";
  res_value = "=" + res_value + "dpi";
  if (base::CompareCaseInsensitiveASCII(res_name, kIppRes) < 0)
    return res_name + res_value + " " + kIppRes + res_value;
  return kIppRes + res_value + " " + res_name + res_value;
}

// By default "Resolution" is used as the PPD resolution attribute name.
std::string ResOpt(std::string res_value) {
  return ResOpt("Resolution", std::move(res_value));
}

std::vector<std::string> GetClientInfoMemberOptions(
    std::string_view client_info_option) {
  EXPECT_GE(client_info_option.size(), 2u);
  EXPECT_EQ(client_info_option.front(), '{');
  EXPECT_EQ(client_info_option.back(), '}');

  std::string_view option_without_braces(client_info_option);
  option_without_braces.remove_prefix(1);
  option_without_braces.remove_suffix(1);

  return base::SplitString(option_without_braces, " ", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY);
}

}  // namespace

// IppToPpd tests

TEST_F(PrintJob, IppToPpd_OutputBin_FaceUpFaceDown) {
  const std::string kPpd = OutputBins({"face-up", "face-down"}).Ppd();
  ASSERT_FALSE(kPpd.empty());
  SetPrinter(kPpd);
  EXPECT_TRUE(CheckOptionSupported("output-bin", "face-up"));
  EXPECT_TRUE(CheckOptionSupported("output-bin", "face-down"));
}

TEST_F(PrintJob, IppToPpd_OutputBin_FaceUp) {
  const std::string kPpd = OutputBins({"face-up"}).Ppd();
  ASSERT_FALSE(kPpd.empty());
  SetPrinter(kPpd);
  EXPECT_TRUE(CheckOptionSupported("output-bin", "face-up"));
  EXPECT_FALSE(CheckOptionSupported("output-bin", "face-down"));
}

TEST_F(PrintJob, IppToPpd_OutputBin_FaceDown) {
  const std::string kPpd = OutputBins({"face-down"}).Ppd();
  ASSERT_FALSE(kPpd.empty());
  SetPrinter(kPpd);
  EXPECT_FALSE(CheckOptionSupported("output-bin", "face-up"));
  EXPECT_TRUE(CheckOptionSupported("output-bin", "face-down"));
}

TEST_F(PrintJob, IppToPpd_OutputBin_Default) {
  const std::string kPpd = Ppd();
  ASSERT_FALSE(kPpd.empty());
  SetPrinter(kPpd);
  EXPECT_FALSE(CheckOptionSupported("output-bin", "face-up"));
  EXPECT_TRUE(CheckOptionSupported("output-bin", "face-down"));
}

TEST_F(PrintJob, IppToPpd_PrintQuality_All) {
  const std::string kPpd =
      PrintQualities({IPP_QUALITY_DRAFT, IPP_QUALITY_NORMAL, IPP_QUALITY_HIGH})
          .Ppd();
  ASSERT_FALSE(kPpd.empty());
  SetPrinter(kPpd);
  EXPECT_TRUE(CheckOptionSupported("print-quality", "3"));
  EXPECT_TRUE(CheckOptionSupported("print-quality", "4"));
  EXPECT_TRUE(CheckOptionSupported("print-quality", "5"));
}

TEST_F(PrintJob, IppToPpd_PrintQuality_High) {
  const std::string kPpd =
      PrintQualities({IPP_QUALITY_NORMAL, IPP_QUALITY_HIGH}).Ppd();
  ASSERT_FALSE(kPpd.empty());
  SetPrinter(kPpd);
  EXPECT_FALSE(CheckOptionSupported("print-quality", "3"));
  EXPECT_TRUE(CheckOptionSupported("print-quality", "4"));
  EXPECT_TRUE(CheckOptionSupported("print-quality", "5"));
}

TEST_F(PrintJob, IppToPpd_PrintQuality_Draft) {
  const std::string kPpd =
      PrintQualities({IPP_QUALITY_NORMAL, IPP_QUALITY_DRAFT}).Ppd();
  ASSERT_FALSE(kPpd.empty());
  SetPrinter(kPpd);
  EXPECT_TRUE(CheckOptionSupported("print-quality", "3"));
  EXPECT_TRUE(CheckOptionSupported("print-quality", "4"));
  EXPECT_FALSE(CheckOptionSupported("print-quality", "5"));
}

TEST_F(PrintJob, IppToPpd_PrintQuality_Default) {
  const std::string kPpd = Ppd();
  ASSERT_FALSE(kPpd.empty());
  SetPrinter(kPpd);
  EXPECT_FALSE(CheckOptionSupported("print-quality", "3"));
  EXPECT_TRUE(CheckOptionSupported("print-quality", "4"));
  EXPECT_FALSE(CheckOptionSupported("print-quality", "5"));
}

TEST_F(PrintJob, IppToPpd_PrintQuality_Normal) {
  const std::string kPpd = PrintQualities({IPP_QUALITY_NORMAL}).Ppd();
  ASSERT_FALSE(kPpd.empty());
  SetPrinter(kPpd);
  EXPECT_FALSE(CheckOptionSupported("print-quality", "3"));
  EXPECT_TRUE(CheckOptionSupported("print-quality", "4"));
  EXPECT_FALSE(CheckOptionSupported("print-quality", "5"));
}

TEST_F(PrintJob, PrintQualityDymoNormalDefault) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenUI *DymoPrintQuality/Print Quality: PickOne
*OrderDependency: 21 AnySetup *DymoPrintQuality
*DefaultDymoPrintQuality: Text
*DymoPrintQuality Text/Text Only: ""
*DymoPrintQuality Graphics/Barcodes and Graphics: ""
*CloseUI: *DymoPrintQuality)");
  EXPECT_FALSE(CheckOptionSupported("print-quality", "3"));
  EXPECT_TRUE(CheckOptionSupported("print-quality", "4"));
  EXPECT_TRUE(CheckOptionSupported("print-quality", "5"));

  EXPECT_EQ("DymoPrintQuality=Text", PrintQuality("4").Filter());
  EXPECT_EQ("DymoPrintQuality=Graphics", PrintQuality("5").Filter());
  EXPECT_EQ(IPP_QUALITY_NORMAL, DefaultPrintQuality());
}

TEST_F(PrintJob, PrintQualityDymoGraphicsDefault) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenUI *DymoPrintQuality/Print Quality: PickOne
*OrderDependency: 21 AnySetup *DymoPrintQuality
*DefaultDymoPrintQuality: Graphics
*DymoPrintQuality Text/Text Only: ""
*DymoPrintQuality Graphics/Barcodes and Graphics: ""
*CloseUI: *DymoPrintQuality)");
  EXPECT_FALSE(CheckOptionSupported("print-quality", "3"));
  EXPECT_TRUE(CheckOptionSupported("print-quality", "4"));
  EXPECT_TRUE(CheckOptionSupported("print-quality", "5"));

  EXPECT_EQ("DymoPrintQuality=Text", PrintQuality("4").Filter());
  EXPECT_EQ("DymoPrintQuality=Graphics", PrintQuality("5").Filter());
  EXPECT_EQ(IPP_QUALITY_HIGH, DefaultPrintQuality());
}

// Roll printing trim tests

TEST_F(PrintJob, RollPrintingTrimEpson) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenUI *TmxPaperCut/Paper Cut: PickOne
*OrderDependency: 30 AnySetup *TmxPaperCut
*DefaultTmxPaperCut: NoCut
*TmxPaperCut NoCut/No cut: ""
*TmxPaperCut CutPerJob/Cut per job: ""
*TmxPaperCut CutPerPage/Cut per page: ""
*CloseUI: *TmxPaperCut)");
  EXPECT_TRUE(CheckOptionSupported("finishings", "3"));
  EXPECT_TRUE(CheckOptionSupported("finishings", "11"));

  EXPECT_EQ("TmxPaperCut=NoCut", Finishings("none").Filter());
  EXPECT_EQ("TmxPaperCut=CutPerJob", Finishings("trim").Filter());
  EXPECT_EQ(IPP_FINISHINGS_NONE, DefaultFinishings());
}

TEST_F(PrintJob, RollPrintingTrimStarPatialCut) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenUI *DocCutType/2. Document Cut Type: PickOne
*DefaultDocCutType: 1PartialCutDoc
*DocCutType 0NoCutDoc/No Cut: ""
*DocCutType 1PartialCutDoc/Partial Cut: ""
*DocCutType 2FullCutDoc/Full Cut: ""
*CloseUI: *DocCutType)");
  EXPECT_TRUE(CheckOptionSupported("finishings", "3"));
  EXPECT_TRUE(CheckOptionSupported("finishings", "11"));

  EXPECT_EQ("DocCutType=0NoCutDoc", Finishings("none").Filter());
  EXPECT_EQ("DocCutType=1PartialCutDoc", Finishings("trim").Filter());
  EXPECT_EQ(IPP_FINISHINGS_TRIM, DefaultFinishings());
}

TEST_F(PrintJob, RollPrintingTrimStarOptionFullCut) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenUI *DocCutType/2. Document Cut Type: PickOne
*DefaultDocCutType: 0NoCutDoc
*DocCutType 0NoCutDoc/No Cut: ""
*DocCutType 2FullCutDoc/Full Cut: ""
*CloseUI: *DocCutType)");
  EXPECT_TRUE(CheckOptionSupported("finishings", "3"));
  EXPECT_TRUE(CheckOptionSupported("finishings", "11"));

  EXPECT_EQ("DocCutType=0NoCutDoc", Finishings("none").Filter());
  EXPECT_EQ("DocCutType=2FullCutDoc", Finishings("trim").Filter());
  EXPECT_EQ(IPP_FINISHINGS_NONE, DefaultFinishings());
}

TEST_F(PrintJob, RollPrintingTrimStarOneOption) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenUI *DocCutType/2. Document Cut Type: PickOne
*DefaultDocCutType: 2FullCutDoc
*DocCutType 2FullCutDoc/Full Cut: ""
*CloseUI: *DocCutType)");
  // If the PPD only has a single choice, we don't support any finishings.
  EXPECT_FALSE(CheckOptionSupported("finishings", "3"));
  EXPECT_FALSE(CheckOptionSupported("finishings", "11"));
}

TEST_F(PrintJob, RollPrintingTrimCustomPartialCut) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenUI *CutterMode/Cutter Mode: PickOne
*DefaultCutterMode: 2TotalCutPage
*CutterMode 0NoCutPage/No Cut: ""
*CutterMode 3PartialCutDoc/Partial cut at the end of the document: ""
*CutterMode 4TotalCutDoc/Total cut at the end of the document: ""
*CloseUI: *CutterMode)");
  EXPECT_TRUE(CheckOptionSupported("finishings", "3"));
  EXPECT_TRUE(CheckOptionSupported("finishings", "11"));

  EXPECT_EQ("CutterMode=0NoCutPage", Finishings("none").Filter());
  EXPECT_EQ("CutterMode=3PartialCutDoc", Finishings("trim").Filter());
  EXPECT_EQ(IPP_FINISHINGS_NONE, DefaultFinishings());
}

TEST_F(PrintJob, RollPrintingTrimCustomFullCut) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenUI *CutterMode/Cutter Mode: PickOne
*DefaultCutterMode: 2FullCutEndDoc
*CutterMode 0NoCut/No cut: ""
*CutterMode 1FullCutEndPage/Full Cut at Page End: ""
*CutterMode 2FullCutEndDoc/Full Cut at Document End: ""
*CloseUI: *CutterMode)");
  EXPECT_TRUE(CheckOptionSupported("finishings", "3"));
  EXPECT_TRUE(CheckOptionSupported("finishings", "11"));

  EXPECT_EQ("CutterMode=0NoCut", Finishings("none").Filter());
  EXPECT_EQ("CutterMode=2FullCutEndDoc", Finishings("trim").Filter());
  EXPECT_EQ(IPP_FINISHINGS_TRIM, DefaultFinishings());
}

TEST_F(PrintJob, RollPrintingTrimHwasung) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenUI *CutterMode/Cutter Mode (Page/Job): PickOne
*DefaultCutterMode: 3PartialPartial
*CutterMode 0NoNo/No Cut / No Cut: ""
*CutterMode 1NoPartial/No Cut / Partial Cut: ""
*CloseUI: *CutterMode)");
  EXPECT_TRUE(CheckOptionSupported("finishings", "3"));
  EXPECT_TRUE(CheckOptionSupported("finishings", "11"));

  EXPECT_EQ("CutterMode=0NoNo", Finishings("none").Filter());
  EXPECT_EQ("CutterMode=1NoPartial", Finishings("trim").Filter());
  EXPECT_EQ(IPP_FINISHINGS_NONE, DefaultFinishings());
}

TEST_F(PrintJob, RollPrintingTrimBrother) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenUI *BrCutAtEnd/Cut at end: PickOne
*OrderDependency: 21 AnySetup  *BrCutAtEnd
*DefaultBrCutAtEnd: ON
*BrCutAtEnd OFF/OFF: "          "
*BrCutAtEnd ON/ON: "          "
*CloseUI: *BrCutAtEnd)");
  EXPECT_TRUE(CheckOptionSupported("finishings", "3"));
  EXPECT_TRUE(CheckOptionSupported("finishings", "11"));

  EXPECT_EQ("BrCutAtEnd=OFF", Finishings("none").Filter());
  EXPECT_EQ("BrCutAtEnd=ON", Finishings("trim").Filter());
  EXPECT_EQ(IPP_FINISHINGS_TRIM, DefaultFinishings());
}

TEST_F(PrintJob, RollPrintingTrimTSC) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenUI *PostAction/Post-Print Action: PickOne
*OrderDependency: 130 AnySetup *PostAction
*DefaultPostAction: TearOff
*PostAction None/None: "%%"
*PostAction TearOff/Tear Off: "%%"
*PostAction PeelOff/Peel Off: "%%"
*PostAction Cut/Cut: "%%"
*PostAction PartialCut/Partial Cut: "%%"
*CloseUI: *PostAction)");
  EXPECT_TRUE(CheckOptionSupported("finishings", "3"));
  EXPECT_TRUE(CheckOptionSupported("finishings", "11"));

  EXPECT_EQ("PostAction=None", Finishings("none").Filter());
  EXPECT_EQ("PostAction=PartialCut", Finishings("trim").Filter());
  EXPECT_EQ(IPP_FINISHINGS_NONE, DefaultFinishings());
}

// PinPrint tests

TEST_F(PrintJob, PinPrint_HP) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*JCLOpenUI *HPPinPrnt/Secure Printing: PickOne
*HPPinPrnt True/On: "%%"
*JCLCloseUI: *HPPinPrnt
*CustomHPDigit True: "@PJL SET HOLDKEY = <22>\1<220A>"
*ParamCustomHPDigit Custom/Custom Name: 1 string 4 32)");
  EXPECT_TRUE(CheckOptionSupported("job-password", "1234"));
  EXPECT_EQ("HPDigit=Custom.1234 HPPinPrnt=True", Password("1234").Filter());
}

TEST_F(PrintJob, PinPrint_Lexmark) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenGroup: JCL/JCL
*ParamCustomPnH pin/Pin Number: 1 passcode 0 4
*CloseGroup: JCL)");
  EXPECT_TRUE(CheckOptionSupported("job-password", "1234"));
  EXPECT_EQ("PnH=Custom.1234", Password("1234").Filter());
}

TEST_F(PrintJob, PinPrint_RicohPassword) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*JCLOpenUI *JobType/JobType: PickOne
*JobType LockedPrint/Locked Print: "@PJL SECUREJOB<0A>"
*JCLCloseUI: *JobType
*CustomPassword True/Custom Password: "@PJL SET JOBPASSWORD2=<22>\1<22><0A>"
*ParamCustomPassword Password: 1 passcode 4 8)");
  EXPECT_TRUE(CheckOptionSupported("job-password", "1234"));
  EXPECT_EQ("JobType=LockedPrint Password=Custom.1234",
            Password("1234").Filter());
}

TEST_F(PrintJob, PinPrint_RicohJobPassword) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenUI *JobType/Job Type: PickOne
*JobType LockedPrint/Locked Print: "%% FoomaticRIPOptionSetting: JobType=LockedPrint"
*End
*CloseUI: *JobType
*CustomJobPassword True/Option: "JobPassword=Custom"
*ParamCustomJobPassword Custom/Custom: 1 passcode 4 8)");
  EXPECT_TRUE(CheckOptionSupported("job-password", "1234"));
  EXPECT_EQ("JobPassword=Custom.1234 JobType=LockedPrint",
            Password("1234").Filter());
}

TEST_F(PrintJob, PinPrint_RicohLockedPrintPassword) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenUI *JobType/JobType: PickOne
*JobType LockedPrint/Locked Print: "%% FoomaticRIPOptionSetting: JobType=LockedPrint"
*End
*CloseUI: *JobType
*CustomLockedPrintPassword True/Custom Password: ""
*ParamCustomLockedPrintPassword Password: 1 password 4 8)");
  EXPECT_TRUE(CheckOptionSupported("job-password", "1234"));
  EXPECT_EQ("JobType=LockedPrint LockedPrintPassword=Custom.1234",
            Password("1234").Filter());
}

TEST_F(PrintJob, PinPrint_Sharp) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*JCLOpenUI *JCLRetentionSetting/Document Filing: PickOne
*JCLRetentionSetting Before/Hold Only:		"@PJL SET HOLD = STORE<0A>"
*JCLCloseUI: *JCLRetentionSetting
*CustomJCLRetentionPassword True: "@PJL SET HOLDTYPE = PRIVATE<0A>@PJL SET HOLDKEY = "\1"<0A>"
*ParamCustomJCLRetentionPassword Password/ : 1 passcode 4 5)");
  EXPECT_TRUE(CheckOptionSupported("job-password", "1234"));
  EXPECT_EQ("JCLRetentionPassword=Custom.1234 JCLRetentionSetting=Before",
            Password("1234").Filter());
}

TEST_F(PrintJob, PinPrint_Unsupported) {
  SetPrinter("*PPD-Adobe: 4.3");
  EXPECT_FALSE(CheckOptionSupported("job-password", "1234"));
  EXPECT_EQ("job-password=1234", Password("1234").Filter());
}

// IppPpdResolutionMapping tests

TEST_F(PrintJob, IppPpdResolutionMapping_ResolutionTagNames) {
  const std::string kTestResNames[] = {"Resolution",     "JCLResolution",
                                       "SetResolution",  "CNRes_PGP",
                                       "HPPrintQuality", "LXResolution"};
  for (const std::string& res_name : kTestResNames) {
    SetPrinter(GeneratePpd(res_name, {"600dpi"}, "600dpi"));
    EXPECT_TRUE(Filter().empty());
    EXPECT_EQ(ResOpt(res_name, "600"), Resolution(600).Filter());
    EXPECT_TRUE(CheckOptionSupported("printer-resolution", "600dpi"));
    EXPECT_EQ("600dpi", DefaultResolution());
  }
}

TEST_F(PrintJob, IppPpdResolutionMapping_NonstandardPpdResolution) {
  SetPrinter(GeneratePpd("Resolution", {"600x600dpi"}));
  EXPECT_TRUE(Filter().empty());
  EXPECT_EQ(ResOpt("600x600"), Resolution(600).Filter());
  EXPECT_TRUE(CheckOptionSupported("printer-resolution", "600dpi"));
  EXPECT_TRUE(DefaultResolution().empty());

  SetPrinter(GeneratePpd("Resolution", {"600x600dpi"}, "600x600dpi"));
  EXPECT_TRUE(Filter().empty());
  EXPECT_EQ(ResOpt("600x600"), Resolution(600).Filter());
  EXPECT_TRUE(CheckOptionSupported("printer-resolution", "600dpi"));
  EXPECT_EQ("600dpi", DefaultResolution());
}

// If no valid resolutions are included in the PPD file, the IPP
// printer-resolution tag should be passed directly to the print filters.
// The default resolution tag should not affect this behavior.
//
// DefaultResolution is valid without Resolution, however this is not
// the case for the other PPD resolution tags such as JCLResolution.
// See the PPD spec v4.3 section 5.9 for more information as well as
// the comments in scheduler/printers.c.
TEST_F(PrintJob, IppPpdResolutionMapping_NoPpdResolution) {
  SetPrinter(GeneratePpd("Resolution"));
  EXPECT_TRUE(Filter().empty());
  EXPECT_EQ("printer-resolution=600dpi", Resolution(600).Filter());
  EXPECT_EQ("printer-resolution=600x300dpi", Resolution(600, 300).Filter());
  EXPECT_TRUE(CheckOptionSupported("printer-resolution", "300dpi"));
  EXPECT_EQ("300dpi", DefaultResolution());

  SetPrinter(GeneratePpd("Resolution", {}, "600dpi"));
  EXPECT_TRUE(Filter().empty());
  EXPECT_EQ("printer-resolution=600dpi", Resolution(600).Filter());
  EXPECT_EQ("printer-resolution=600x300dpi", Resolution(600, 300).Filter());
  EXPECT_TRUE(CheckOptionSupported("printer-resolution", "600dpi"));
  EXPECT_EQ("600dpi", DefaultResolution());

  SetPrinter(GeneratePpd("JCLResolution", {}, "600dpi"));
  EXPECT_TRUE(Filter().empty());
  EXPECT_EQ("printer-resolution=600dpi", Resolution(600).Filter());
  EXPECT_EQ("printer-resolution=600x300dpi", Resolution(600, 300).Filter());
  EXPECT_FALSE(CheckOptionSupported("printer-resolution", "600dpi"));
  EXPECT_TRUE(CheckOptionSupported("printer-resolution", "300dpi"));
  EXPECT_EQ("300dpi", DefaultResolution());
}

TEST_F(PrintJob, IppPpdResolutionMapping_IppPpdResolutionMismatch) {
  SetPrinter(GeneratePpd("Resolution", {"600dpi"}));
  EXPECT_TRUE(Resolution(300).Filter().empty());
  EXPECT_TRUE(Resolution(300, 600).Filter().empty());
  EXPECT_TRUE(Resolution(600, 300).Filter().empty());
  SetPrinter(GeneratePpd("Resolution", {"300x600dpi"}));
  EXPECT_TRUE(Resolution(300).Filter().empty());
  EXPECT_TRUE(Resolution(600, 300).Filter().empty());
  EXPECT_TRUE(Resolution(600).Filter().empty());
}

TEST_F(PrintJob, IppPpdResolutionMapping_MultiplePpdResolutions) {
  SetPrinter(GeneratePpd("Resolution", {"300dpi", "300x600dpi", "600dpi"},
                         "600x300dpi"));
  EXPECT_TRUE(Filter().empty());
  EXPECT_EQ(ResOpt("300"), Resolution(300).Filter());
  EXPECT_EQ(ResOpt("300x600"), Resolution(300, 600).Filter());
  EXPECT_EQ(ResOpt("600"), Resolution(600).Filter());
  EXPECT_TRUE(Resolution(600, 300).Filter().empty());

  EXPECT_TRUE(CheckOptionSupported("printer-resolution", "300dpi"));
  EXPECT_TRUE(CheckOptionSupported("printer-resolution", "300x600dpi"));
  EXPECT_TRUE(CheckOptionSupported("printer-resolution", "600dpi"));
  EXPECT_FALSE(CheckOptionSupported("printer-resolution", "600x300dpi"));
  EXPECT_TRUE(DefaultResolution().empty());
}

TEST_F(PrintJob, IppClientInfoToOptionMapping) {
  SetPrinter("*PPD-Adobe: 4.3");
  EXPECT_TRUE(Filter().empty());
  std::string opt_string =
      ClientInfos(
          {{"a", "b", "c", "d", 3}, {"d", "c", std::nullopt, std::nullopt, 4}})
          .Filter();
  ASSERT_THAT(opt_string, testing::StartsWith("client-info="));

  std::vector<std::string> values =
      base::SplitString(std::string_view(opt_string.data() + 12), ",",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  ASSERT_EQ(values.size(), 2);
  EXPECT_THAT(
      GetClientInfoMemberOptions(values[0]),
      testing::UnorderedElementsAre(
          "client-name=\"a\"", "client-type=3", "client-string-version=\"b\"",
          "client-patches=\"d\"", "client-version=\"c\""));
  EXPECT_THAT(
      GetClientInfoMemberOptions(values[1]),
      testing::UnorderedElementsAre("client-name=\"d\"", "client-type=4",
                                    "client-string-version=\"c\""));
}

// This is a regression test for http://b/265760617.
TEST_F(PrintJob, IppClientInfoWithOtherOptions) {
  SetPrinter("*PPD-Adobe: 4.3");
  EXPECT_TRUE(Filter().empty());
  std::string opt_string = ClientInfos({
                                           {"a", "b", "c", "d", 3},
                                       })
                               .Password("1234")
                               .Filter();
  size_t client_info_end_pos = opt_string.find('}');
  ASSERT_NE(client_info_end_pos, std::string::npos);
  const std::string_view client_info_opt(opt_string.data(),
                                         client_info_end_pos + 1);
  ASSERT_THAT(client_info_opt, testing::StartsWith("client-info="));

  const std::string_view client_info_opt_value = client_info_opt.substr(12);
  EXPECT_THAT(
      GetClientInfoMemberOptions(client_info_opt_value),
      testing::UnorderedElementsAre(
          "client-name=\"a\"", "client-type=3", "client-string-version=\"b\"",
          "client-patches=\"d\"", "client-version=\"c\""));

  size_t job_password_start_pos =
      opt_string.find("job-password", client_info_end_pos);
  ASSERT_NE(job_password_start_pos, std::string::npos);
  const std::string_view job_password_opt(opt_string.data() +
                                          job_password_start_pos);
  EXPECT_EQ(job_password_opt, "job-password=1234");
}
