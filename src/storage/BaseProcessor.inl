/* Copyright (c) 2018 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include "base/Base.h"
#include "storage/BaseProcessor.h"

namespace nebula {
namespace storage {

template<typename RESP>
cpp2::ErrorCode BaseProcessor<RESP>::to(kvstore::ResultCode code) {
    switch (code) {
    case kvstore::ResultCode::SUCCEEDED:
        return cpp2::ErrorCode::SUCCEEDED;
    case kvstore::ResultCode::ERR_LEADER_CHANGED:
        return cpp2::ErrorCode::E_LEADER_CHANGED;
    case kvstore::ResultCode::ERR_SPACE_NOT_FOUND:
        return cpp2::ErrorCode::E_SPACE_NOT_FOUND;
    case kvstore::ResultCode::ERR_PART_NOT_FOUND:
        return cpp2::ErrorCode::E_PART_NOT_FOUND;
    default:
        return cpp2::ErrorCode::E_UNKNOWN;
    }
}


template<typename RESP>
void BaseProcessor<RESP>::doPut(GraphSpaceID spaceId,
                                PartitionID partId,
                                std::vector<kvstore::KV> data) {
    this->kvstore_->asyncMultiPut(spaceId,
                                  partId,
                                  std::move(data),
                                  [spaceId, partId, this](kvstore::ResultCode code) {
        VLOG(3) << "partId:" << partId << ", code:" << static_cast<int32_t>(code);

        cpp2::ResultCode thriftResult;
        thriftResult.set_code(to(code));
        thriftResult.set_part_id(partId);
        if (code == kvstore::ResultCode::ERR_LEADER_CHANGED) {
            nebula::cpp2::HostAddr leader;
            auto addrRet = kvstore_->partLeader(spaceId, partId);
            CHECK(ok(addrRet));
            auto addr = value(std::move(addrRet));
            leader.set_ip(addr.first);
            leader.set_port(addr.second);
            thriftResult.set_leader(leader);
        }
        bool finished = false;
        {
            std::lock_guard<std::mutex> lg(this->lock_);
            if (thriftResult.code != cpp2::ErrorCode::SUCCEEDED) {
                this->codes_.emplace_back(std::move(thriftResult));
            }
            this->callingNum_--;
            if (this->callingNum_ == 0) {
                result_.set_failed_codes(std::move(this->codes_));
                finished = true;
            }
        }
        if (finished) {
            this->onFinished();
        }
    });
}

}  // namespace storage
}  // namespace nebula
