build_lib(
  LIBNAME batman
  SOURCE_FILES
    model/batman-header.cc
    model/batman-routing-protocol.cc
    helper/batman-helper.cc
  HEADER_FILES
    model/batman-header.h
    model/batman-routing-protocol.h
    helper/batman-helper.h
  LIBRARIES_TO_LINK
    ${libinternet}
    ${libwifi}
  TEST_SOURCES
    test/batman-test-suite.cc
  TEST_LIBRARIES_TO_LINK
    ${libflow-monitor}
)
