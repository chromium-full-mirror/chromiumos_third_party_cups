// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include "cupsd.h"
}

#include <string>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "brillo/file_utils.h"
#include "gtest/gtest.h"

namespace {

constexpr char kPrinter[] = "thefake";
const base::FilePath kPpdPath =
    base::FilePath("conf/ppd").Append(kPrinter).AddExtension("ppd");

class PrintJob : public testing::Test {
 public:
  PrintJob() : job_(cupsdAddJob(0, "")){};

  ~PrintJob() {
    // It is considered successful to attempt to delete a file that
    // does not exist.
    EXPECT_TRUE(base::DeleteFile(kPpdPath, false));
    if (job_)
      cupsdDeleteJob(job_, CUPSD_JOB_PURGE);
  }

  PrintJob(const PrintJob&) = delete;
  PrintJob& operator=(const PrintJob&) = delete;

  void SetUp() {
    ASSERT_TRUE(job_);
    job_->attrs = ippNew();
    ASSERT_TRUE(job_->attrs);
  }

  // Use the PPD string ppd_data to set printer capabilities.
  void SetPrinter(const std::string& ppd_data) const {
    ASSERT_STREQ("conf", PrinterRoot);
    if (job_->printer)
      cupsdDeletePrinter(job_->printer, true);
    job_->printer = cupsdAddPrinter(kPrinter);
    ASSERT_TRUE(job_->printer);
    job_->printer->temporary = true;
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
    AddIpp("job-password", password);
    return *this;
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

 private:
  // We need separate void implementations of functions so that the
  // ASSERT macro can be used.
  void ResolutionImpl(int res_x, int res_y) const {
    ASSERT_TRUE(job_->attrs);
    ASSERT_TRUE(ippAddResolution(job_->attrs, IPP_TAG_JOB, "printer-resolution",
                                 IPP_RES_PER_INCH, res_x, res_y));
  }

  void FilterImpl(std::string& out) const {
    ASSERT_TRUE(job_->printer);
    ASSERT_TRUE(job_->attrs);
    // &nul, a valid memory address, is used instead of NULL.
    // This is necessary because otherwise strlcpy(NULL, "1", 0) segfaults
    // in job.c.
    char nul = 0;
    char* args = get_options(job_, false, &nul, 0, &nul, 0);
    ASSERT_TRUE(args);
    out = args;
    free(args);
    if (out.size() && out[0] == ' ')
      out = out.substr(1);
  }

  void AddIpp(const std::string& name, const std::string& value) const {
    ASSERT_TRUE(job_->attrs);
    ASSERT_TRUE(ippAddString(job_->attrs, IPP_TAG_JOB, IPP_TAG_TEXT,
                             name.c_str(), NULL, value.c_str()));
  }

  cupsd_job_t* const job_;
};

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

std::string ResOpt(std::string res_value) {
  return ResOpt("Resolution", std::move(res_value));
}

}  // namespace

TEST_F(PrintJob, PinPrint_HP) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*JCLOpenUI *HPPinPrnt/Secure Printing: PickOne
*HPPinPrnt True/On: "%%"
*JCLCloseUI: *HPPinPrnt
*CustomHPDigit True: "@PJL SET HOLDKEY = <22>\1<220A>"
*ParamCustomHPDigit Custom/Custom Name: 1 string 4 32)");
  EXPECT_EQ("HPDigit=Custom.1234 HPPinPrnt=True", Password("1234").Filter());
}

TEST_F(PrintJob, PinPrint_Lexmark) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*OpenGroup: JCL/JCL
*ParamCustomPnH pin/Pin Number: 1 passcode 0 4
*CloseGroup: JCL)");
  EXPECT_EQ("PnH=Custom.1234", Password("1234").Filter());
}

TEST_F(PrintJob, PinPrint_RicohPassword) {
  SetPrinter(R"(*PPD-Adobe: 4.3
*JCLOpenUI *JobType/JobType: PickOne
*JobType LockedPrint/Locked Print: "@PJL SECUREJOB<0A>"
*JCLCloseUI: *JobType
*CustomPassword True/Custom Password: "@PJL SET JOBPASSWORD2=<22>\1<22><0A>"
*ParamCustomPassword Password: 1 passcode 4 8)");
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
  EXPECT_EQ("JCLRetentionPassword=Custom.1234 JCLRetentionSetting=Before",
            Password("1234").Filter());
}

TEST_F(PrintJob, PinPrint_Unsupported) {
  SetPrinter("*PPD-Adobe: 4.3");
  EXPECT_EQ("job-password=1234", Password("1234").Filter());
}

TEST_F(PrintJob, IppPpdResolutionMapping_ResolutionTagNames) {
  const std::string kTestResNames[] = {"Resolution",     "JCLResolution",
                                       "SetResolution",  "CNRes_PGP",
                                       "HPPrintQuality", "LXResolution"};
  for (const std::string& res_name : kTestResNames) {
    SetPrinter(GeneratePpd(res_name, {"600dpi"}));
    EXPECT_TRUE(Filter().empty());
    EXPECT_EQ(ResOpt(res_name, "600"), Resolution(600).Filter());
  }
}

TEST_F(PrintJob, IppPpdResolutionMapping_NonstandardPpdResolution) {
  SetPrinter(GeneratePpd("Resolution", {"600x600dpi"}));
  EXPECT_TRUE(Filter().empty());
  EXPECT_EQ(ResOpt("600x600"), Resolution(600).Filter());
}

// If no valid resolutions are included in the PPD file, the IPP
// printer-resolution tag should be passed directly to the print filters.
// The default resolution tag should not affect this behavior.
TEST_F(PrintJob, IppPpdResolutionMapping_NoPpdResolution) {
  SetPrinter(GeneratePpd("Resolution"));
  EXPECT_TRUE(Filter().empty());
  EXPECT_EQ("printer-resolution=600dpi", Resolution(600).Filter());
  EXPECT_EQ("printer-resolution=600x300dpi", Resolution(600, 300).Filter());

  SetPrinter(GeneratePpd("Resolution", {"600dpcm"}));
  EXPECT_TRUE(Filter().empty());
  EXPECT_EQ("printer-resolution=600dpi", Resolution(600).Filter());
  EXPECT_EQ("printer-resolution=600x300dpi", Resolution(600, 300).Filter());

  SetPrinter(GeneratePpd("Resolution", {}, "600dpi"));
  EXPECT_TRUE(Filter().empty());
  EXPECT_EQ("printer-resolution=600dpi", Resolution(600).Filter());
  EXPECT_EQ("printer-resolution=600x300dpi", Resolution(600, 300).Filter());
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
}
