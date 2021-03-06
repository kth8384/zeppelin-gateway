#include "src/s3_cmds/zgw_s3_command.h"

#include "src/zgw_utils.h"
#include "src/s3_cmds/zgw_s3_object.h"
#include "src/s3_cmds/zgw_s3_bucket.h"
#include "src/s3_cmds/zgw_s3_xml.h"
#include "src/zgwstore/zgw_define.h"

class ZgwTestCmd : public S3Cmd {
 public:
  ZgwTestCmd(int flags)
      : S3Cmd(flags) {
  }

  virtual bool DoInitial() {
    http_response_xml_.clear();
    return true;
  }
  virtual void DoAndResponse(pink::HTTPResponse* resp) {

    resp->SetStatusCode(200);
    resp->SetContentLength(http_response_xml_.size());
  }
  virtual int DoResponseBody(char* buf, size_t max_size) {
    if (max_size < http_response_xml_.size()) {
      memcpy(buf, http_response_xml_.data(), max_size);
      http_response_xml_.assign(http_response_xml_.substr(max_size));
    } else {
      memcpy(buf, http_response_xml_.data(), http_response_xml_.size());
    }
    return std::min(max_size, http_response_xml_.size());
  };
};

class UnImplementCmd : public S3Cmd {
 public:
  UnImplementCmd (int flags)
      : S3Cmd(flags) {
  }

  virtual bool DoInitial() {
    return false;
  }
  virtual void DoAndResponse(pink::HTTPResponse* resp) {
    GenerateErrorXml(kNotImplemented);
    resp->SetStatusCode(501);
    resp->SetContentLength(http_response_xml_.size());
  }
  virtual int DoResponseBody(char* buf, size_t max_size) {
    if (max_size < http_response_xml_.size()) {
      memcpy(buf, http_response_xml_.data(), max_size);
      http_response_xml_.assign(http_response_xml_.substr(max_size));
    } else {
      memcpy(buf, http_response_xml_.data(), http_response_xml_.size());
    }
    return std::min(max_size, http_response_xml_.size());
  };
};

void InitCmdTable(S3CmdTable* cmd_table) {
  cmd_table->insert(
    std::make_pair(kListAllBuckets, new ListAllBucketsCmd(kFlagsRead)));
  cmd_table->insert(
    std::make_pair(kDeleteBucket, new DeleteBucketCmd(kFlagsWrite)));
  cmd_table->insert(
    std::make_pair(kListObjects, new ListObjectsCmd(kFlagsRead)));
  cmd_table->insert(
    std::make_pair(kGetBucketLocation, new GetBucketLocationCmd(kFlagsRead)));
  cmd_table->insert(
    std::make_pair(kHeadBucket, new HeadBucketCmd(kFlagsRead)));
  cmd_table->insert(
    std::make_pair(kListMultiPartUpload, new ListMultiPartUploadCmd(kFlagsRead)));
  cmd_table->insert(
    std::make_pair(kPutBucket, new PutBucketCmd(kFlagsWrite)));
  cmd_table->insert(
    std::make_pair(kDeleteObject, new DeleteObjectCmd(kFlagsWrite)));
  cmd_table->insert(
    std::make_pair(kDeleteMultiObjects, new DeleteMultiObjectsCmd(kFlagsWrite)));
  cmd_table->insert(
    std::make_pair(kGetObject, new GetObjectCmd(kFlagsRead)));
  cmd_table->insert(
    std::make_pair(kHeadObject, new HeadObjectCmd(kFlagsRead)));
  cmd_table->insert(
    std::make_pair(kPostObject, new PostObjectCmd(kFlagsWrite)));
  cmd_table->insert(
    std::make_pair(kPutObject, new PutObjectCmd(kFlagsWrite)));
  cmd_table->insert(
    std::make_pair(kPutObjectCopy, new PutObjectCopyCmd(kFlagsWrite)));
  cmd_table->insert(
    std::make_pair(kInitMultipartUpload, new InitMultipartUploadCmd(kFlagsWrite)));
  cmd_table->insert(
    std::make_pair(kUploadPart, new UploadPartCmd(kFlagsWrite)));
  cmd_table->insert(
    std::make_pair(kUploadPartCopy, new UploadPartCopyCmd(kFlagsWrite)));
  cmd_table->insert(
    std::make_pair(kUploadPartCopyPartial, new UploadPartCopyPartialCmd(kFlagsWrite)));
  cmd_table->insert(
    std::make_pair(kCompleteMultiUpload, new CompleteMultiUploadCmd(kFlagsWrite)));
  cmd_table->insert(
    std::make_pair(kAbortMultiUpload, new AbortMultiUploadCmd(kFlagsWrite)));
  cmd_table->insert(
    std::make_pair(kListParts, new ListPartsCmd(kFlagsRead)));
  cmd_table->insert(
    std::make_pair(kUnImplement, new UnImplementCmd(kFlagsRead)));
  cmd_table->insert(
    std::make_pair(kZgwTest, new ZgwTestCmd(kFlagsRead | kFlagsWrite)));
}

void DestroyCmdTable(S3CmdTable* cmd_table) {
  for (auto& cmd : *cmd_table) {
    delete cmd.second;
  }
}

bool S3Cmd::TryAuth() {
  if (flags() == kFlagsRead &&
      !bucket_name_.empty() &&
      req_headers_.count("authorization") == 0 &&
      query_params_.count("X-Amz-Signature") == 0) {
    zgwstore::Bucket bkt;
    std::string unuseful_name;
    bool anonymous = true;
    Status s = store_->GetBucket(unuseful_name, bucket_name_, &bkt, anonymous);
    if (s.ok() && bkt.acl == "PUBLIC") {
      user_name_.assign(bkt.owner);
      return true;
    }
  }
  switch(s3_auth_.TryAuth()) {
    case kAuthSuccess:
      http_ret_code_ = 200;
      break;
    case kAccessKeyInvalid:
      http_ret_code_ = 403;
      GenerateErrorXml(kInvalidAccessKeyId);
      return false;
      break;
    case kSignatureNotMatch:
      http_ret_code_ = 403;
      GenerateErrorXml(kSignatureDoesNotMatch);
      return false;
      break;
    case kMaybeAuthV2:
    default:
      http_ret_code_ = 400;
      GenerateErrorXml(kInvalidRequest, "Please use AWS4-HMAC-SHA256.");
      return false;
  }

  user_name_.assign(s3_auth_.user_name());

  return true;
}

void S3Cmd::Clear() {
  user_name_.clear();
  bucket_name_.clear();
  object_name_.clear();
  req_headers_.clear();
  query_params_.clear();
  store_ = nullptr;

  http_ret_code_ = 200;
  http_request_xml_.clear();
  http_response_xml_.clear();
}

void S3Cmd::GenerateErrorXml(S3ErrorType type, const std::string& message) {
  http_response_xml_.clear();
  S3XmlDoc doc("Error");
  switch(type) {
    case kInvalidRequest:
      doc.AppendToRoot(doc.AllocateNode("Code", "InvalidRequest"));
      doc.AppendToRoot(doc.AllocateNode("Message", message));
      break;
    case kAccessDenied:
      doc.AppendToRoot(doc.AllocateNode("Code", "AccessDenied"));
      doc.AppendToRoot(doc.AllocateNode("Message", "Access Denied"));
      break;
    case kInvalidRange:
      doc.AppendToRoot(doc.AllocateNode("Code", "InvalidRange"));
      doc.AppendToRoot(doc.AllocateNode("ObjectName", message));
      break;
    case kInvalidArgument:
      doc.AppendToRoot(doc.AllocateNode("Code", "InvalidArgument"));
      doc.AppendToRoot(doc.AllocateNode("Message", "Copy Source must mention the "
                                        "source bucket and key: "
                                        "sourcebucket/sourcekey"));
      doc.AppendToRoot(doc.AllocateNode("ArgumentName", message));
      break;
    case kMethodNotAllowed:
      doc.AppendToRoot(doc.AllocateNode("Code", "MethodNotAllowed"));
      doc.AppendToRoot(doc.AllocateNode("Message", "The specified method is not "
                                        "allowed against this resource."));
      break;
    case kInvalidPartOrder:
      doc.AppendToRoot(doc.AllocateNode("Code", "InvalidPartOrder"));
      doc.AppendToRoot(doc.AllocateNode("Message", "The list of parts was not in "
                                        "ascending order. The parts list must be "
                                        "specified in order by part number."));
      break;
    case kInvalidPart:
      doc.AppendToRoot(doc.AllocateNode("Code", "InvalidPart"));
      doc.AppendToRoot(doc.AllocateNode("Message", "One or more of the specified "
                                        "parts could not be found. The part might "
                                        "not have been uploaded, or the specified "
                                        "entity tag might not have matched the "
                                        "part's entity tag."));
      break;
    case kMalformedXML:
      doc.AppendToRoot(doc.AllocateNode("Code", "MalformedXML"));
      doc.AppendToRoot(doc.AllocateNode("Message", "This happens when the user "
                                        "sends malformed xml (xml that doesn't "
                                                             "conform to the "
                                                             "published xsd) for "
                                        "the configuration. The error message is, "
                                        "\"The XML you provided was not well-formed "
                                        "or did not validate against our published "
                                        "schema.\""));
      break;
    case kNoSuchUpload:
      doc.AppendToRoot(doc.AllocateNode("Code", "NoSuchUpload"));
      doc.AppendToRoot(doc.AllocateNode("Message", "The specified upload does not "
                                        "exist. The upload ID may be invalid, or "
                                        "the upload may have been aborted or "
                                        "completed."));
      doc.AppendToRoot(doc.AllocateNode("UploadId", message));
      break;
    case kBucketAlreadyExists:
      doc.AppendToRoot(doc.AllocateNode("Code", "BucketAlreadyExists"));
      doc.AppendToRoot(doc.AllocateNode("Message", "The requested bucket name is "
                                        "not available. The bucket namespace is "
                                        "shared by all users of the system. Please "
                                        "select a different name and try again."));
      break;
    case kBucketAlreadyOwnedByYou:
      doc.AppendToRoot(doc.AllocateNode("Code", "BucketAlreadyOwnedByYou"));
      doc.AppendToRoot(doc.AllocateNode("Message", "Your previous request to "
                                        "create the named bucket succeeded and you "
                                        "already own it."));
      break;
    case kInvalidAccessKeyId:
      doc.AppendToRoot(doc.AllocateNode("Code", "InvalidAccessKeyId"));
      doc.AppendToRoot(doc.AllocateNode("Message", "The access key Id you provided "
                                        "does not exist in our records."));
      break;
    case kNotImplemented:
      doc.AppendToRoot(doc.AllocateNode("Code", "NotImplemented"));
      doc.AppendToRoot(doc.AllocateNode("Message", "A header you provided implies "
                                        "functionality that is not implemented."));
      break;
    case kSignatureDoesNotMatch:
      doc.AppendToRoot(doc.AllocateNode("Code", "SignatureDoesNotMatch"));
      doc.AppendToRoot(doc.AllocateNode("Message", "The request signature we "
                                        "calculated does not match the signature "
                                        "you provided. Check your key and signing "
                                        "method."));
      break;
    case kNoSuchBucket:
      doc.AppendToRoot(doc.AllocateNode("Code", "NoSuchBucket"));
      doc.AppendToRoot(doc.AllocateNode("Message", "The specified bucket does not "
                                        "exist."));
      doc.AppendToRoot(doc.AllocateNode("BucketName", message));
      break;
    case kNoSuchKey:
      doc.AppendToRoot(doc.AllocateNode("Code", "NoSuchKey"));
      doc.AppendToRoot(doc.AllocateNode("Message", "The specified key does not "
                                        "exist."));
      doc.AppendToRoot(doc.AllocateNode("Key", message));
      break;
    case kBucketNotEmpty:
      doc.AppendToRoot(doc.AllocateNode("Code", "BucketNotEmpty"));
      doc.AppendToRoot(doc.AllocateNode("Message", "The bucket you tried to delete "
                                        "is not empty"));
      doc.AppendToRoot(doc.AllocateNode("BucketName", message));
      break;
    default:
      break;
  }
  doc.AppendToRoot("RequestId", request_id_);
  doc.AppendToRoot("HostId", md5(hostname()));
  doc.ToString(&http_response_xml_);
}
