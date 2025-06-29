/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The Gecko Profiler is an always-on profiler that takes fast and low overhead
// samples of the program execution using only userspace functionality for
// portability. The goal of this module is to provide performance data in a
// generic cross-platform way without requiring custom tools or kernel support.
//
// Samples are collected to form a timeline with optional timeline event
// (markers) used for filtering. The samples include both native stacks and
// platform-independent "label stack" frames.

#ifndef ProfileAdditionalInformation_h
#define ProfileAdditionalInformation_h

#include "SharedLibraries.h"
#include "js/Value.h"
#include "nsString.h"

namespace IPC {
class MessageReader;
class MessageWriter;
template <typename T>
struct ParamTraits;
}  // namespace IPC

namespace mozilla {
using ProfilerJSSources =
    std::unordered_map<int64_t, std::unordered_map<uint32_t, std::string>>;

// This structure contains additional information gathered while generating the
// profile json and iterating the buffer.
struct ProfileGenerationAdditionalInformation {
  ProfileGenerationAdditionalInformation() = default;
  explicit ProfileGenerationAdditionalInformation(
      const SharedLibraryInfo&& aSharedLibraries,
      const ProfilerJSSources&& aJSSources)
      : mSharedLibraries(aSharedLibraries), mJSSources(aJSSources) {}

  size_t SizeOf() const {
    size_t size = mSharedLibraries.SizeOf();

    // Add size of mJSSources
    for (const auto& processPair : mJSSources) {
      size += sizeof(processPair.first);   // int64_t key
      size += sizeof(processPair.second);  // unordered_map overhead
      for (const auto& sourcePair : processPair.second) {
        size += sizeof(sourcePair.first);  // uint32_t key
        size += sourcePair.second.size();  // string content
      }
    }

    return size;
  }

  void Append(ProfileGenerationAdditionalInformation&& aOther) {
    mSharedLibraries.AddAllSharedLibraries(aOther.mSharedLibraries);
    mJSSources.merge(aOther.mJSSources);
  }

  void FinishGathering() { mSharedLibraries.DeduplicateEntries(); }

  void ToJSValue(JSContext* aCx, JS::MutableHandle<JS::Value> aRetVal) const;

  SharedLibraryInfo mSharedLibraries;
  ProfilerJSSources mJSSources;
};

struct ProfileAndAdditionalInformation {
  ProfileAndAdditionalInformation() = default;
  explicit ProfileAndAdditionalInformation(const nsCString&& aProfile)
      : mProfile(aProfile) {}

  ProfileAndAdditionalInformation(
      const nsCString&& aProfile,
      const ProfileGenerationAdditionalInformation&& aAdditionalInformation)
      : mProfile(aProfile),
        mAdditionalInformation(Some(aAdditionalInformation)) {}

  size_t SizeOf() const {
    size_t size = mProfile.Length();
    if (mAdditionalInformation.isSome()) {
      size += mAdditionalInformation->SizeOf();
    }
    return size;
  }

  nsCString mProfile;
  Maybe<ProfileGenerationAdditionalInformation> mAdditionalInformation;
};
}  // namespace mozilla

namespace IPC {
template <>
struct ParamTraits<mozilla::ProfileGenerationAdditionalInformation> {
  typedef mozilla::ProfileGenerationAdditionalInformation paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam);
  static bool Read(MessageReader* aReader, paramType* aResult);
};
}  // namespace IPC

#endif  // ProfileAdditionalInformation_h
