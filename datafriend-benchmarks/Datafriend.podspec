Pod::Spec.new do |s|
  s.name = 'Datafriend'
  s.version = '0.1.0'
  s.summary = 'Independent native persistence and synchronous query views'
  s.homepage = 'https://github.com/arasskin/DrawDo'
  s.author = 'DrawDo contributors'
  s.license = { :type => 'EPL-1.0', :file => 'NOTICE.md' }
  s.source = { :git => 'https://github.com/arasskin/DrawDo.git' }
  s.platform = :ios, '13.0'
  s.source_files = 'native/*.{cpp,c,h}', 'include/datafriend/*.{h}'
  s.private_header_files = 'native/*.h'
  s.public_header_files = 'include/datafriend/c_api.h'
  s.libraries = 'c++'
  s.frameworks = 'Security', 'CoreFoundation'
  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++20',
    'GCC_PREPROCESSOR_DEFINITIONS' => '$(inherited) MDB_HASH_SIZE=8',
    'HEADER_SEARCH_PATHS' => '$(inherited) "${PODS_TARGET_SRCROOT}/include" "${PODS_TARGET_SRCROOT}/../third_party/monocypher" "${PODS_TARGET_SRCROOT}/../third_party/aelmdb/src" "${PODS_TARGET_SRCROOT}/../third_party/unordered_dense/include" "${PODS_TARGET_SRCROOT}/../littlebear-benchmarks/src"',
    'DEAD_CODE_STRIPPING' => 'NO'
  }
end
