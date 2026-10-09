#ifdef __APPLE__
#include "datafriend/identity.h"
#include <Security/Security.h>
#include <stdexcept>
#include <string>
#include <cstring>

namespace datafriend {
namespace {
template<class T> struct cf_owner {
  T value;
  ~cf_owner() { if(value) CFRelease(value); }
};
void check(OSStatus status) {
  if(status != errSecSuccess)
    throw std::runtime_error("Datafriend Keychain error " + std::to_string(status));
}
}
void identity_load_or_create(std::string_view alias, uint8_t seed[32]) {
  cf_owner account{CFStringCreateWithBytes(nullptr,
    reinterpret_cast<const UInt8*>(alias.data()), alias.size(), kCFStringEncodingUTF8, false)};
  if(!account.value) throw std::bad_alloc{};
  cf_owner query{CFDictionaryCreateMutable(nullptr, 0,
    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks)};
  if(!query.value) throw std::bad_alloc{};
  CFDictionarySetValue(query.value, kSecClass, kSecClassGenericPassword);
  CFDictionarySetValue(query.value, kSecAttrService, CFSTR("dev.drawdo.datafriend.ed25519.v1"));
  CFDictionarySetValue(query.value, kSecAttrAccount, account.value);
  CFDictionarySetValue(query.value, kSecAttrSynchronizable, kCFBooleanFalse);
  auto load = [&]() {
    CFDictionarySetValue(query.value, kSecReturnData, kCFBooleanTrue);
    CFTypeRef result = nullptr;
    const auto status = SecItemCopyMatching(query.value, &result);
    cf_owner data{result};
    CFDictionaryRemoveValue(query.value, kSecReturnData);
    if(status == errSecItemNotFound) return false;
    check(status);
    if(!result || CFGetTypeID(result) != CFDataGetTypeID() ||
       CFDataGetLength(static_cast<CFDataRef>(result)) != 32)
      throw std::runtime_error("invalid Datafriend Keychain identity");
    std::memcpy(seed, CFDataGetBytePtr(static_cast<CFDataRef>(result)), 32);
    return true;
  };
  if(load()) return;
  // No second plaintext copy; the native seed owner wipes this memory on return.
  cf_owner secret{CFDataCreateWithBytesNoCopy(nullptr, seed, 32, kCFAllocatorNull)};
  if(!secret.value) throw std::bad_alloc{};
  CFDictionarySetValue(query.value, kSecValueData, secret.value);
  CFDictionarySetValue(query.value, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
  const auto status = SecItemAdd(query.value, nullptr);
  CFDictionaryRemoveValue(query.value, kSecValueData);
  CFDictionaryRemoveValue(query.value, kSecAttrAccessible);
  if(status == errSecDuplicateItem) {
    if(!load()) throw std::runtime_error("identity disappeared during creation");
  } else check(status);
}
}
#endif
