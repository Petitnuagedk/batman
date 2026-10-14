build_lib_example(
  NAME batman-chain
  SOURCE_FILES batman-chain.cc
  LIBRARIES_TO_LINK ${libbatman} ${libinternet} ${libwifi} ${libmobility} ${libapplications}
)
