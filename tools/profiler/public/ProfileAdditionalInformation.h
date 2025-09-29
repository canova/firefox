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
#include "js/Utility.h"
#include "js/ProfilingSources.h"
#include "mozilla/Unused.h"
#include "mozilla/Variant.h"
#include "nsString.h"
#include "mozilla/HashTable.h"
#include "nsTStringHasher.h"

namespace IPC {
class MessageReader;
class MessageWriter;
template <typename T>
struct ParamTraits;
}  // namespace IPC

namespace mozilla {

// Ergonomic version of ProfilerJSSourceData for IPC serialization
struct ErgonomicProfilerJSSourceData {
  uint32_t sourceId;
  nsCString filePath;
  Maybe<Variant<nsString, nsCString, ProfilerJSSourceData::RetrievableFile>>
      source;

  ErgonomicProfilerJSSourceData() = default;

  // Constructor from ProfilerJSSourceData
  explicit ErgonomicProfilerJSSourceData(const ProfilerJSSourceData& aData)
      : sourceId(aData.sourceId()) {
    if (aData.filePathLength() > 0) {
      filePath.Assign(aData.filePath(), aData.filePathLength());
    }

    aData.data().match(
        [&](const ProfilerJSSourceData::SourceTextUTF16& srcText) {
          // Use nsDependentString to wrap the existing UTF-16 data without
          // copying
          source = Some(Variant<nsString, nsCString,
                                ProfilerJSSourceData::RetrievableFile>(
              nsString(nsDependentString(
                  reinterpret_cast<const char16_t*>(srcText.chars_.get()),
                  srcText.length_))));
        },
        [&](const ProfilerJSSourceData::SourceTextUTF8& srcText) {
          // Use nsDependentCString to wrap the existing UTF-8 data without
          // copying
          source =
              Some(Variant<nsString, nsCString,
                           ProfilerJSSourceData::RetrievableFile>(nsCString(
                  nsDependentCString(srcText.chars_.get(), srcText.length_))));
        },
        [&](const ProfilerJSSourceData::RetrievableFile&) {
          source = Some(Variant<nsString, nsCString,
                                ProfilerJSSourceData::RetrievableFile>(
              ProfilerJSSourceData::RetrievableFile{}));
        },
        [&](const ProfilerJSSourceData::Unavailable&) {
          // source remains None for unavailable sources
        });
  }

  // No copy constructors/assignment
  ErgonomicProfilerJSSourceData(const ErgonomicProfilerJSSourceData&) = delete;
  ErgonomicProfilerJSSourceData& operator=(
      const ErgonomicProfilerJSSourceData&) = delete;

  // Move constructors/assignment
  ErgonomicProfilerJSSourceData(ErgonomicProfilerJSSourceData&&) = default;
  ErgonomicProfilerJSSourceData& operator=(ErgonomicProfilerJSSourceData&&) =
      default;

  size_t SizeOf() const {
    // Size of sourceId + filepath
    size_t size = sizeof(uint32_t) + filePath.Length();
    if (source.isSome()) {
      source->match(
          [&](const nsString& str) { size += str.Length() * sizeof(char16_t); },
          [&](const nsCString& str) { size += str.Length(); },
          [&](const ProfilerJSSourceData::RetrievableFile&) {
            /* no extra size */
          });
    }
    return size;
  }
};

// Variant that can hold either the original ProfilerJSSourceData or the
// ergonomic version
using JSSourceDataVariant =
    mozilla::Variant<ProfilerJSSourceData, ErgonomicProfilerJSSourceData>;

// Maps UUID strings to JS source data for WebChannel requests
using JSSourcesByUUID = mozilla::HashMap<nsCString, JSSourceDataVariant>;

// This structure contains additional information gathered while generating
// the profile json and iterating the buffer.
struct ProfileGenerationAdditionalInformation {
  ProfileGenerationAdditionalInformation() = default;
  // Constructor for compatibility with existing code that passes
  // ProfilerJSSourceData
  explicit ProfileGenerationAdditionalInformation(
      SharedLibraryInfo&& aSharedLibraries,
      mozilla::HashMap<nsCString, ProfilerJSSourceData>&& aJSSourcesByUUID)
      : mSharedLibraries(std::move(aSharedLibraries)) {
    // Convert ProfilerJSSourceData to variant form
    for (auto iter = aJSSourcesByUUID.iter(); !iter.done(); iter.next()) {
      mozilla::Unused << mJSSourcesByUUID.put(
          iter.get().key(), JSSourceDataVariant(std::move(iter.get().value())));
    }
  }

  // Constructor that accepts the variant type directly
  explicit ProfileGenerationAdditionalInformation(
      SharedLibraryInfo&& aSharedLibraries, JSSourcesByUUID&& aJSSourcesByUUID)
      : mSharedLibraries(std::move(aSharedLibraries)),
        mJSSourcesByUUID(std::move(aJSSourcesByUUID)) {}

  size_t SizeOf() const {
    size_t size = mSharedLibraries.SizeOf();

    for (auto iter = mJSSourcesByUUID.iter(); !iter.done(); iter.next()) {
      const nsCString& uuid = iter.get().key();
      const JSSourceDataVariant& sourceData = iter.get().value();
      size += uuid.Length();

      sourceData.match(
          [&](const ProfilerJSSourceData& data) { size += data.SizeOf(); },
          [&](const ErgonomicProfilerJSSourceData& data) {
            size += sizeof(uint32_t) + data.filePath.Length();
            if (data.source.isSome()) {
              data.source->match(
                  [&](const nsString& str) {
                    size += str.Length() * sizeof(char16_t);
                  },
                  [&](const nsCString& str) { size += str.Length(); },
                  [&](const ProfilerJSSourceData::
                          RetrievableFile&) { /* no extra size */ });
            }
          });
    }

    return size;
  }

  ProfileGenerationAdditionalInformation(
      const ProfileGenerationAdditionalInformation& other) = delete;
  ProfileGenerationAdditionalInformation& operator=(
      const ProfileGenerationAdditionalInformation&) = delete;

  ProfileGenerationAdditionalInformation(
      ProfileGenerationAdditionalInformation&& other) = default;
  ProfileGenerationAdditionalInformation& operator=(
      ProfileGenerationAdditionalInformation&& other) = default;

  void Append(ProfileGenerationAdditionalInformation&& aOther) {
    mSharedLibraries.AddAllSharedLibraries(aOther.mSharedLibraries);

    for (auto iter = aOther.mJSSourcesByUUID.iter(); !iter.done();
         iter.next()) {
      mozilla::Unused << mJSSourcesByUUID.put(iter.get().key(),
                                              std::move(iter.get().value()));
    }
  }

  void FinishGathering() { mSharedLibraries.DeduplicateEntries(); }

  void ToJSValue(JSContext* aCx, JS::MutableHandle<JS::Value> aRetVal) const;

  friend IPC::ParamTraits<mozilla::ProfileGenerationAdditionalInformation>;

 private:
  JSString* CreateJSStringFromSourceData(
      JSContext* aCx, const ProfilerJSSourceData& aSourceData) const;
  JSString* CreateJSStringFromErgonomicData(
      JSContext* aCx, const ErgonomicProfilerJSSourceData& aData) const;

  SharedLibraryInfo mSharedLibraries;
  JSSourcesByUUID mJSSourcesByUUID;
};

struct ProfileAndAdditionalInformation {
  ProfileAndAdditionalInformation() = default;
  explicit ProfileAndAdditionalInformation(nsCString&& aProfile)
      : mProfile(std::move(aProfile)) {}

  ProfileAndAdditionalInformation(
      nsCString&& aProfile,
      ProfileGenerationAdditionalInformation&& aAdditionalInformation)
      : mProfile(std::move(aProfile)),
        mAdditionalInformation(Some(std::move(aAdditionalInformation))) {}

  ProfileAndAdditionalInformation(const ProfileAndAdditionalInformation&) =
      delete;
  ProfileAndAdditionalInformation& operator=(
      const ProfileAndAdditionalInformation&) = delete;

  ProfileAndAdditionalInformation(ProfileAndAdditionalInformation&&) = default;
  ProfileAndAdditionalInformation& operator=(
      ProfileAndAdditionalInformation&&) = default;

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
