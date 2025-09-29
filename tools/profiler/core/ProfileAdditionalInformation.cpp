/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProfileAdditionalInformation.h"

#include "jsapi.h"
#include "js/JSON.h"
#include "js/PropertyAndElement.h"
#include "js/Value.h"
#include "mozilla/Assertions.h"
#include "mozilla/JSONStringWriteFuncs.h"
#include "mozilla/ipc/IPDLParamTraits.h"

#ifdef MOZ_GECKO_PROFILER
#  include "platform.h"

JSString*
mozilla::ProfileGenerationAdditionalInformation::CreateJSStringFromSourceData(
    JSContext* aCx, const ProfilerJSSourceData& aSourceData) const {
  return aSourceData.data().match(
      [&](const ProfilerJSSourceData::SourceTextUTF16& srcText) -> JSString* {
        return JS_NewUCStringCopyN(aCx, srcText.chars_.get(), srcText.length_);
      },
      [&](const ProfilerJSSourceData::SourceTextUTF8& srcText) -> JSString* {
        return JS_NewStringCopyN(aCx, srcText.chars_.get(), srcText.length_);
      },
      [&](const ProfilerJSSourceData::RetrievableFile&) -> JSString* {
        ProfilerJSSourceData retrievedData =
            js::RetrieveProfilerSourceContent(aCx, aSourceData.filePath());
        const auto& data = retrievedData.data();
        MOZ_RELEASE_ASSERT(data.is<ProfilerJSSourceData::SourceTextUTF8>(),
                           "Retrieved JS source has to be utf-8");

        const auto& srcText = data.as<ProfilerJSSourceData::SourceTextUTF8>();
        return JS_NewStringCopyN(aCx, srcText.chars_.get(), srcText.length_);
      },
      [&](const ProfilerJSSourceData::Unavailable&) -> JSString* {
        return JS_NewStringCopyZ(aCx, "[unavailable]");
      });
}

JSString* mozilla::ProfileGenerationAdditionalInformation::
    CreateJSStringFromErgonomicData(
        JSContext* aCx, const ErgonomicProfilerJSSourceData& aData) const {
  if (!aData.source.isSome()) {
    return JS_NewStringCopyZ(aCx, "[unavailable]");
  }

  return aData.source->match(
      [&](const nsString& str) -> JSString* {
        return JS_NewUCStringCopyN(aCx, str.get(), str.Length());
      },
      [&](const nsCString& str) -> JSString* {
        return JS_NewStringCopyN(aCx, str.get(), str.Length());
      },
      [&](const ProfilerJSSourceData::RetrievableFile&) -> JSString* {
        ProfilerJSSourceData retrievedData =
            js::RetrieveProfilerSourceContent(aCx, aData.filePath.get());

        const auto& data = retrievedData.data();
        MOZ_RELEASE_ASSERT(data.is<ProfilerJSSourceData::SourceTextUTF8>(),
                           "Retrieved JS source has to be utf-8");

        const auto& srcText = data.as<ProfilerJSSourceData::SourceTextUTF8>();
        return JS_NewStringCopyN(aCx, srcText.chars_.get(), srcText.length_);
      });
}

void mozilla::ProfileGenerationAdditionalInformation::ToJSValue(
    JSContext* aCx, JS::MutableHandle<JS::Value> aRetVal) const {
  // Get the shared libraries array.
  JS::Rooted<JS::Value> sharedLibrariesVal(aCx);
  {
    JSONStringWriteFunc<nsCString> buffer;
    JSONWriter w(buffer, JSONWriter::SingleLineStyle);
    w.StartArrayElement();
    AppendSharedLibraries(w, mSharedLibraries);
    w.EndArray();
    NS_ConvertUTF8toUTF16 buffer16(buffer.StringCRef());
    MOZ_ALWAYS_TRUE(JS_ParseJSON(aCx,
                                 static_cast<const char16_t*>(buffer16.get()),
                                 buffer16.Length(), &sharedLibrariesVal));
  }

  // Create jsSources object, which is UUID to source text mapping for
  // WebChannel.
  JS::Rooted<JSObject*> jsSourcesObj(aCx, JS_NewPlainObject(aCx));
  if (jsSourcesObj) {
    for (auto iter = mJSSourcesByUUID.iter(); !iter.done(); iter.next()) {
      const nsCString& uuid = iter.get().key();
      const JSSourceDataVariant& sourceData = iter.get().value();

      JSString* sourceStr = sourceData.match(
          [&](const ProfilerJSSourceData& data) -> JSString* {
            return CreateJSStringFromSourceData(aCx, data);
          },
          [&](const ErgonomicProfilerJSSourceData& data) -> JSString* {
            return CreateJSStringFromErgonomicData(aCx, data);
          });

      if (sourceStr) {
        JS::Rooted<JS::Value> sourceVal(aCx, JS::StringValue(sourceStr));
        JS_SetProperty(aCx, jsSourcesObj, PromiseFlatCString(uuid).get(),
                       sourceVal);
      }
    }
  }

  JS::Rooted<JSObject*> additionalInfoObj(aCx, JS_NewPlainObject(aCx));
  JS::Rooted<JS::Value> jsSourcesVal(aCx, JS::ObjectValue(*jsSourcesObj));
  JS_SetProperty(aCx, additionalInfoObj, "sharedLibraries", sharedLibrariesVal);
  JS_SetProperty(aCx, additionalInfoObj, "jsSources", jsSourcesVal);
  aRetVal.setObject(*additionalInfoObj);
}
#endif  // MOZ_GECKO_PROFILER

namespace IPC {

template <>
struct ParamTraits<SharedLibrary> {
  typedef SharedLibrary paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam);
  static bool Read(MessageReader* aReader, paramType* aResult);
};

template <>
struct ParamTraits<SharedLibraryInfo> {
  typedef SharedLibraryInfo paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam);
  static bool Read(MessageReader* aReader, paramType* aResult);
};

template <>
struct ParamTraits<ProfilerJSSourceData::RetrievableFile> {
  typedef ProfilerJSSourceData::RetrievableFile paramType;

  // Empty struct, nothing to write
  static void Write(MessageWriter* aWriter, const paramType& aParam) {}

  // Empty struct, nothing to read
  static bool Read(MessageReader* aReader, paramType* aResult) { return true; }
};

template <>
struct ParamTraits<mozilla::ErgonomicProfilerJSSourceData> {
  typedef mozilla::ErgonomicProfilerJSSourceData paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam);
  static bool Read(MessageReader* aReader, paramType* aResult);
};

void IPC::ParamTraits<SharedLibrary>::Write(MessageWriter* aWriter,
                                            const paramType& aParam) {
  WriteParam(aWriter, aParam.mStart);
  WriteParam(aWriter, aParam.mEnd);
  WriteParam(aWriter, aParam.mOffset);
  WriteParam(aWriter, aParam.mBreakpadId);
  WriteParam(aWriter, aParam.mCodeId);
  WriteParam(aWriter, aParam.mModuleName);
  WriteParam(aWriter, aParam.mModulePath);
  WriteParam(aWriter, aParam.mDebugName);
  WriteParam(aWriter, aParam.mDebugPath);
  WriteParam(aWriter, aParam.mVersion);
  WriteParam(aWriter, aParam.mArch);
}

bool IPC::ParamTraits<SharedLibrary>::Read(MessageReader* aReader,
                                           paramType* aResult) {
  return ReadParam(aReader, &aResult->mStart) &&
         ReadParam(aReader, &aResult->mEnd) &&
         ReadParam(aReader, &aResult->mOffset) &&
         ReadParam(aReader, &aResult->mBreakpadId) &&
         ReadParam(aReader, &aResult->mCodeId) &&
         ReadParam(aReader, &aResult->mModuleName) &&
         ReadParam(aReader, &aResult->mModulePath) &&
         ReadParam(aReader, &aResult->mDebugName) &&
         ReadParam(aReader, &aResult->mDebugPath) &&
         ReadParam(aReader, &aResult->mVersion) &&
         ReadParam(aReader, &aResult->mArch);
}

void IPC::ParamTraits<SharedLibraryInfo>::Write(MessageWriter* aWriter,
                                                const paramType& aParam) {
  paramType& p = const_cast<paramType&>(aParam);
  WriteParam(aWriter, p.mEntries);
}

bool IPC::ParamTraits<SharedLibraryInfo>::Read(MessageReader* aReader,
                                               paramType* aResult) {
  return ReadParam(aReader, &aResult->mEntries);
}

void IPC::ParamTraits<mozilla::ErgonomicProfilerJSSourceData>::Write(
    MessageWriter* aWriter, const paramType& aParam) {
  WriteParam(aWriter, aParam.sourceId);
  WriteParam(aWriter, aParam.filePath);
  WriteParam(aWriter, aParam.source);
}

bool IPC::ParamTraits<mozilla::ErgonomicProfilerJSSourceData>::Read(
    MessageReader* aReader, paramType* aResult) {
  return ReadParam(aReader, &aResult->sourceId) &&
         ReadParam(aReader, &aResult->filePath) &&
         ReadParam(aReader, &aResult->source);
}

void IPC::ParamTraits<mozilla::ProfileGenerationAdditionalInformation>::Write(
    MessageWriter* aWriter, const paramType& aParam) {
  WriteParam(aWriter, aParam.mSharedLibraries);

  WriteParam(aWriter, static_cast<uint32_t>(aParam.mJSSourcesByUUID.count()));
  for (auto iter = aParam.mJSSourcesByUUID.iter(); !iter.done(); iter.next()) {
    const nsCString& uuid = iter.get().key();
    const JSSourceDataVariant& sourceData = iter.get().value();
    WriteParam(aWriter, uuid);

    // Always serialize as ErgonomicProfilerJSSourceData for IPC
    sourceData.match(
        [&](const ProfilerJSSourceData& data) {
          mozilla::ErgonomicProfilerJSSourceData ergonomicData(data);
          WriteParam(aWriter, ergonomicData);
        },
        [&](const ErgonomicProfilerJSSourceData& data) {
          WriteParam(aWriter, data);
        });
  }
}

bool IPC::ParamTraits<mozilla::ProfileGenerationAdditionalInformation>::Read(
    MessageReader* aReader, paramType* aResult) {
  if (!ReadParam(aReader, &aResult->mSharedLibraries)) {
    return false;
  }

  uint32_t numSources;
  if (!ReadParam(aReader, &numSources)) {
    return false;
  }

  for (uint32_t i = 0; i < numSources; ++i) {
    nsCString uuid;
    mozilla::ErgonomicProfilerJSSourceData ergonomicData;
    if (!ReadParam(aReader, &uuid) || !ReadParam(aReader, &ergonomicData)) {
      return false;
    }
    // Store as ErgonomicProfilerJSSourceData in the variant (IPC
    // deserialization gives us owned strings)
    if (!aResult->mJSSourcesByUUID.put(
            uuid, JSSourceDataVariant(std::move(ergonomicData)))) {
      return false;
    }
  }

  return true;
}

}  // namespace IPC
