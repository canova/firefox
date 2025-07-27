/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_ProfilingSources_h
#define js_ProfilingSources_h

#include "mozilla/Variant.h"

#include <stdint.h>

#include "jstypes.h"

#include "js/TypeDecls.h"
#include "js/Utility.h"
#include "js/Vector.h"

/*
 * Struct to pass JS source data with content type information for profiler use.
 *
 * Note that both UniqueChars and UniqueTwoByteChars are null-terminated
 * strings.
 */
struct JS_PUBLIC_API ProfilerJSSourceData {
  uint32_t sourceId_;
  // Null-terminated file path for the source.
  JS::UniqueChars filePath_;
  size_t filePathLength_;

  struct SourceTextUTF16 {
    JS::UniqueTwoByteChars chars_;
    size_t length_;

    SourceTextUTF16(JS::UniqueTwoByteChars&& c, size_t l)
        : chars_(std::move(c)), length_(l) {}
  };

  struct SourceTextUTF8 {
    JS::UniqueChars chars_;
    size_t length_;

    SourceTextUTF8(JS::UniqueChars&& c, size_t l)
        : chars_(std::move(c)), length_(l) {}
  };

  /*
   * Represents a source file that can be retrieved later in the parent process.
   * Used when source text is not immediately available in the current process
   * but can be fetched using the file path information.
   */
  struct RetrievableFile {};

  struct Unavailable {};

  using ProfilerSourceVariant =
      mozilla::Variant<SourceTextUTF16, SourceTextUTF8, RetrievableFile,
                       Unavailable>;
  ProfilerSourceVariant data_;

  // Constructors
  ProfilerJSSourceData(uint32_t sourceId, JS::UniqueChars&& filePath,
                       size_t pathLen)
      : sourceId_(sourceId),
        filePath_(std::move(filePath)),
        filePathLength_(pathLen),
        data_(Unavailable{}) {}

  // UTF-8 source text with filePath
  ProfilerJSSourceData(uint32_t sourceId, JS::UniqueChars&& chars,
                       size_t length, JS::UniqueChars&& filePath,
                       size_t pathLen)
      : sourceId_(sourceId),
        filePath_(std::move(filePath)),
        filePathLength_(pathLen),
        data_(SourceTextUTF8{std::move(chars), length}) {}

  // UTF-16 source text with filePath
  ProfilerJSSourceData(uint32_t sourceId, JS::UniqueTwoByteChars&& chars,
                       size_t length, JS::UniqueChars&& filePath,
                       size_t pathLen)
      : sourceId_(sourceId),
        filePath_(std::move(filePath)),
        filePathLength_(pathLen),
        data_(SourceTextUTF16{std::move(chars), length}) {}

  ProfilerJSSourceData()
      : sourceId_(0), filePathLength_(0), data_(Unavailable{}) {}

  static ProfilerJSSourceData CreateRetrievableFile(uint32_t sourceId,
                                                    JS::UniqueChars&& filePath,
                                                    size_t pathLength) {
    ProfilerJSSourceData result(sourceId, std::move(filePath), pathLength);
    result.data_.emplace<RetrievableFile>();
    return result;
  }

  ProfilerJSSourceData(ProfilerJSSourceData&&) = default;
  ProfilerJSSourceData& operator=(ProfilerJSSourceData&&) = default;

  // No copy constructors as this class owns its string storage.
  ProfilerJSSourceData(const ProfilerJSSourceData& other) = delete;
  ProfilerJSSourceData& operator=(const ProfilerJSSourceData&) = delete;

  uint32_t sourceId() const { return sourceId_; }
  const char* filePath() const { return filePath_.get(); }
  size_t filePathLength() const { return filePathLength_; }

  bool isSourceText() const {
    return data_.is<SourceTextUTF16>() || data_.is<SourceTextUTF8>();
  }
  bool isSourceTextUTF16() const { return data_.is<SourceTextUTF16>(); }
  bool isSourceTextUTF8() const { return data_.is<SourceTextUTF8>(); }
  bool isRetrievableFile() const { return data_.is<RetrievableFile>(); }
  bool isUnavailable() const { return data_.is<Unavailable>(); }

  const SourceTextUTF16& asSourceTextUTF16() const {
    return data_.as<SourceTextUTF16>();
  }
  const SourceTextUTF8& asSourceTextUTF8() const {
    return data_.as<SourceTextUTF8>();
  }

  size_t SizeOf() const {
    // Size of sourceId + filepath
    size_t size = sizeof(uint32_t) + filePathLength_ * sizeof(char);

    if (isSourceTextUTF16()) {
      const auto& srcText = asSourceTextUTF16();
      size += srcText.length_ * sizeof(char16_t);
    } else if (isSourceTextUTF8()) {
      const auto& srcText = asSourceTextUTF8();
      size += srcText.length_ * sizeof(char);
    }

    return size;
  }
};

namespace js {

using ProfilerJSSources =
    js::Vector<ProfilerJSSourceData, 0, js::SystemAllocPolicy>;

/*
 * Main API for getting the profiled JS sources.
 */
JS_PUBLIC_API ProfilerJSSources GetProfilerScriptSources(JSContext* cx);

/**
 * Retrieve the JS sources that are only retrievable from the parent process.
 * See RetrievableFile struct for more information.
 * */
JS_PUBLIC_API ProfilerJSSourceData
RetrieveProfilerSourceContent(JSContext* cx, const char* filename);

}  // namespace js

#endif /* js_ProfilingSources_h */
