project(SecuritySocket)
cmake_minimum_required (VERSION 3.8)

# Security Socket Project

message("-- Security Socket --")

## COMMON VARIABLE

set(repository_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../..")
# /.. main
# /../.. cpp
# /../../.. SecuritySocket
# /../../../.. android
# /../../../../.. prj
# /../../../../../.. repository_dir
set(project_dir ${CMAKE_CURRENT_SOURCE_DIR})
message("repository_dir = ${repository_dir}")
message("project_dir = ${project_dir}")

set(source_dir "${repository_dir}/src")
set(library_dir "${repository_dir}/lib")
message("source_dir = ${source_dir}")
message("library_dir = ${library_dir}")

set(build_dir ${project_dir}/../../../libs)
message("build_dir = ${build_dir}")

## OpenSSL

message("-- OpenSSL CMake Initialization --")

set(openssl_source_dir "${library_dir}/openssl")
set(openssl_temp_dir ${CMAKE_CURRENT_BINARY_DIR}/openssl_temp)

set(openssl_build_dir ${build_dir}/openssl)
set(openssl_include_dir ${openssl_build_dir}/include)
set(openssl_binary_dir ${openssl_build_dir}/lib)

message("openssl source dir : ${openssl_source_dir}")
message("openssl_temp_dir : ${openssl_temp_dir}")

message("openssl_build_dir : ${openssl_build_dir}")
message("openssl_include_dir : ${openssl_temp_dir}")
message("openssl_binary_dir : ${openssl_binary_dir}")

### OpenSSL | OpenSSL Version Detection

file( STRINGS "${openssl_source_dir}/include/openssl/opensslv.h" openssl_version_str
  REGEX "^#[\t ]*define[\t ]+OPENSSL_VERSION_NUMBER[\t ]+0x([0-9a-fA-F])+.*" )

function(from_hex HEX DEC)
  string( TOUPPER "${HEX}" HEX )
  set( _res 0 )
  string( LENGTH "${HEX}" _strlen )

  while( _strlen GREATER 0 )
    math( EXPR _res "${_res} * 16" )
    string( SUBSTRING "${HEX}" 0 1 NIBBLE )
    string( SUBSTRING "${HEX}" 1 -1 HEX )
    if( NIBBLE STREQUAL "A" )
      math( EXPR _res "${_res} + 10" )
    elseif( NIBBLE STREQUAL "B" )
      math( EXPR _res "${_res} + 11" )
    elseif( NIBBLE STREQUAL "C" )
      math( EXPR _res "${_res} + 12" )
    elseif( NIBBLE STREQUAL "D" )
      math( EXPR _res "${_res} + 13" )
    elseif( NIBBLE STREQUAL "E" )
      math( EXPR _res "${_res} + 14" )
    elseif( NIBBLE STREQUAL "F" )
      math( EXPR _res "${_res} + 15" )
    else()
      math( EXPR _res "${_res} + ${NIBBLE}" )
    endif()

    string( LENGTH "${HEX}" _strlen )
  endwhile()

  set( ${DEC} ${_res} PARENT_SCOPE )
endfunction()

string( REGEX REPLACE "^.*OPENSSL_VERSION_NUMBER[\t ]+0x([0-9a-fA-F])([0-9a-fA-F][0-9a-fA-F])([0-9a-fA-F][0-9a-fA-F])([0-9a-fA-F][0-9a-fA-F])([0-9a-fA-F]).*$"
  "\\1;\\2;\\3;\\4;\\5" OPENSSL_VERSION_LIST "${openssl_version_str}" )
  list( GET OPENSSL_VERSION_LIST 0 OPENSSL_VERSION_MAJOR )
  list( GET OPENSSL_VERSION_LIST 1 OPENSSL_VERSION_MINOR )
  from_hex( "${OPENSSL_VERSION_MINOR}" OPENSSL_VERSION_MINOR )
  list( GET OPENSSL_VERSION_LIST 2 OPENSSL_VERSION_FIX )
  from_hex( "${OPENSSL_VERSION_FIX}" OPENSSL_VERSION_FIX )
  list( GET OPENSSL_VERSION_LIST 3 OPENSSL_VERSION_PATCH )

if( NOT OPENSSL_VERSION_PATCH STREQUAL "00" )
  from_hex( "${OPENSSL_VERSION_PATCH}" _tmp )
  # 96 is the ASCII code of 'a' minus 1
  math( EXPR OPENSSL_VERSION_PATCH_ASCII "${_tmp} + 96" )
  unset( _tmp )
  string( ASCII "${OPENSSL_VERSION_PATCH_ASCII}" OPENSSL_VERSION_PATCH_STRING )
endif()

set( OPENSSL_VERSION "${OPENSSL_VERSION_MAJOR}.${OPENSSL_VERSION_MINOR}.${OPENSSL_VERSION_FIX}${OPENSSL_VERSION_PATCH_STRING}" )

message( STATUS "OpenSSL version ${OPENSSL_VERSION}" )

set( VERSION_MAJOR ${OPENSSL_VERSION_MAJOR} )
set( VERSION_MINOR ${OPENSSL_VERSION_MINOR} )
set( VERSION_PATCH ${OPENSSL_VERSION_FIX} )

set( VERSION_STRING ${OPENSSL_VERSION} )
set( LIB_VERSION ${VERSION_MAJOR}.${VERSION_MINOR} )
set( LIB_SOVERSION ${VERSION_MAJOR}.${VERSION_MINOR} )

### OpenSSL | Presets by compiler and OS

add_definitions(-DOPENSSL)
add_definitions( -DOPENSSL_NO_ASM )
add_definitions( -DOPENSSL_NO_STATIC_ENGINE )

if( MSVC )
  # include( MSVCRuntime )
  # configure_msvc_runtime()
  set( OPENSSLDIR "C:/ssl" )
  set( ENGINESDIR "C:/engines-1.1" )
else()
  set( OPENSSLDIR "${CMAKE_INSTALL_PREFIX}/ssl" )
  set( ENGINESDIR "${CMAKE_INSTALL_PREFIX}/engines-1.1" )
endif()
add_definitions( "-DOPENSSLDIR=\"${OPENSSLDIR}\"" )
add_definitions( "-DENGINESDIR=\"${ENGINESDIR}\"" )

if( APPLE )
  set( CMAKE_MACOSX_RPATH ON )
  add_definitions( -DOPENSSL_SYSNAME_MACOSX )
endif()

if( WIN32 )
  set( CMAKE_SHARED_LIBRARY_PREFIX "lib" )
  set( CMAKE_STATIC_LIBRARY_PREFIX "lib" )
endif()

if( WIN32 AND NOT CYGWIN )
  add_definitions( -DOPENSSL_SYSNAME_WIN32 )
  add_definitions( -DWIN32_LEAN_AND_MEAN )
  add_definitions( -D_CRT_SECURE_NO_WARNINGS )

  if(BUILD_SHARED_LIBS)
    # avoid conflict: ocsp.h and wincrypt.h
    add_definitions( -D_WINDLL )
  endif()
endif()

if( MINGW )
  set( CMAKE_SHARED_LINKER_FLAGS "-Wl,--export-all" )
endif()

include( CheckTypeSize )
check_type_size( "long" LONG_INT )
check_type_size( "long long" LONG_LONG_INT )
check_type_size( "int" INT )
if( HAVE_LONG_INT AND (${LONG_INT} EQUAL 8) )
  set( SIXTY_FOUR_BIT_LONG ON )
elseif( CMAKE_SIZEOF_VOID_P EQUAL 8 )
  set( SIXTY_FOUR_BIT ON )
else()
  set( THIRTY_TWO_BIT ON )
endif()

if( MSVC OR ( WIN32 AND MINGW AND NOT CYGWIN ) )
  set( OPENSSL_EXPORT_VAR_AS_FUNCTION 1 )
endif()

### OpenSSL | Configure Build Public Header

file( COPY ${openssl_source_dir}/include/internal DESTINATION ${openssl_include_dir} )
file( COPY ${openssl_source_dir}/include/crypto DESTINATION ${openssl_include_dir} )
file( COPY ${openssl_source_dir}/include/openssl DESTINATION ${openssl_include_dir} )

file( READ ${openssl_source_dir}/opensslconf.h.cmake CONF )
set( CONF "
#define OPENSSL_NO_MD2
#define OPENSSL_NO_RC5
#define OPENSSL_NO_RFC3779
#define OPENSSL_NO_EC_NISTP_64_GCC_128
${CONF}" )

file( WRITE ${openssl_temp_dir}/opensslconf.h.cmake "${CONF}" )

configure_file( ${openssl_temp_dir}/opensslconf.h.cmake
  ${openssl_include_dir}/openssl/opensslconf.h )

### OpenSSL Crypto

set(openssl_crypto_source_dir ${openssl_source_dir}/crypto)
set(openssl_crypto_configuration_dir ${openssl_source_dir}/cmake/crypto)
set(openssl_crypto_include_dir "")

message("openssl_crypto_source_dir = ${openssl_crypto_source_dir}")
message("openssl_crypto_configuration_dir = ${openssl_crypto_configuration_dir}")

#### OpenSSL Crypto | Check if fcntl.h exists

include( CMakeParseArguments )

string(TIMESTAMP BUILDINF_DATE "%Y-%m-%d %H:%M:%S UTC" UTC)
configure_file( ${openssl_crypto_configuration_dir}/buildinf.h.cmake ${openssl_temp_dir}/buildinf.h )

include( CheckIncludeFile )

CHECK_INCLUDE_FILE( fcntl.h CHK_DLFCN )
if( CHK_DLFCN )
  set( HAVE_DLFCN_H ON )
endif()

#### OpenSSL Crypto | Set Library Extension 

if( APPLE )
  set( DSO_DLFCN ON )
  set( DSO_EXTENSION ".dylib" )
elseif( WIN32 AND NOT CYGWIN )
  set( DSO_WIN32 ON )
  set( DSO_EXTENSION ".dll" )
elseif( CYGWIN )
  set( DSO_DLFCN ON )
  set( DSO_EXTENSION ".dll" )
else()
  if( CHK_DLFCN )
    set( DSO_DLFCN ON )
  else()
    set( DSO_NONE ON )
  endif()
  set( DSO_EXTENSION ".so" )
endif()

#### OpenSSL Crypto | Configure Include File

configure_file( ${openssl_crypto_configuration_dir}/bn_conf.h.cmake
  ${openssl_include_dir}/crypto/bn_conf.h )
configure_file( ${openssl_crypto_configuration_dir}/dso_conf.h.cmake
  ${openssl_include_dir}/crypto/dso_conf.h )

#### OpenSSL Crypto | Make Include Directories for using openssl_crypto and openssl_ssl

set(openssl_crypto_include_dir "${openssl_crypto_source_dir}/modes"
    "${openssl_crypto_source_dir}/ec/curve448"
    "${openssl_crypto_source_dir}/ec/curve448/arch_32"
    "${openssl_temp_dir}"
    "${openssl_include_dir}"
    "${openssl_source_dir}")

message("openssl_crypto_include_dir is ${openssl_crypto_include_dir}") 

##### When it not works, using this
# include_directories( BEFORE SYSTEM
#   "${openssl_crypto_source_dir}/modes" 
# "${openssl_crypto_source_dir}/ec/curve448" 
# "${openssl_crypto_source_dir}/ec/curve448/arch_32"
#   ${openssl_temp_dir}/ # buildinf.h
#   ${openssl_include_dir}/ # public|crypto|internal
#   ${openssl_source_dir} # e_os.h
# )

#### OpenSSL Crypto | Find Source Files

macro( add_submodule dir )
  set( options )
  set( oneValueArgs )
  set( multiValueArgs EXHEADERS )
  cmake_parse_arguments( add_submodule "" "" "${multiValueArgs}" ${ARGN} )

  message( STATUS "{dir} ${dir}" )
  # message( STATUS "{add_submodule_UNPARSED_ARGUMENTS} ${add_submodule_UNPARSED_ARGUMENTS}" )
  foreach( name ${add_submodule_UNPARSED_ARGUMENTS} )
      set( openssl_crypto_source_files ${openssl_crypto_source_files} ${dir}/${name} )
  endforeach( name )

  # message( STATUS "{add_submodule_EXHEADERS} ${add_submodule_EXHEADERS}" )
  foreach( name ${add_submodule_EXHEADERS} )
      set( EXHEADERS ${EXHEADERS} ${name} )
  endforeach( name )

endmacro( add_submodule )

add_submodule( ${openssl_crypto_source_dir}
  cpt_err.c cryptlib.c ctype.c cversion.c ebcdic.c ex_data.c init.c mem.c mem_clr.c mem_dbg.c mem_sec.c o_dir.c o_fips.c
  o_fopen.c o_init.c o_str.c o_time.c uid.c getenv.c )
  
add_submodule( ${openssl_crypto_source_dir}
  cpt_err.c cryptlib.c ctype.c cversion.c ebcdic.c ex_data.c init.c mem.c mem_clr.c mem_dbg.c mem_sec.c o_dir.c o_fips.c
  o_fopen.c o_init.c o_str.c o_time.c uid.c getenv.c )


add_submodule ( ${openssl_crypto_source_dir}/aes aes_cbc.c aes_cfb.c aes_core.c aes_ecb.c aes_ige.c aes_misc.c aes_ofb.c
  aes_wrap.c )

add_submodule ( ${openssl_crypto_source_dir}/aria aria.c )

add_submodule ( ${openssl_crypto_source_dir}/asn1 a_bitstr.c a_d2i_fp.c a_digest.c a_dup.c a_gentm.c a_i2d_fp.c a_int.c
  a_mbstr.c a_object.c a_octet.c a_print.c a_sign.c a_strex.c a_strnid.c a_time.c a_type.c
  a_utctm.c a_utf8.c a_verify.c ameth_lib.c asn1_err.c asn1_gen.c asn1_item_list.c asn1_lib.c asn1_par.c
  asn_mime.c asn_moid.c asn_mstbl.c asn_pack.c bio_asn1.c bio_ndef.c d2i_pr.c d2i_pu.c
  evp_asn1.c f_int.c f_string.c i2d_pr.c i2d_pu.c n_pkey.c nsseq.c p5_pbe.c p5_pbev2.c
  p5_scrypt.c p8_pkey.c t_bitst.c t_pkey.c t_spki.c tasn_dec.c tasn_enc.c tasn_fre.c
  tasn_new.c tasn_prn.c tasn_scn.c tasn_typ.c tasn_utl.c x_algor.c x_bignum.c x_info.c
  x_int64.c x_long.c x_pkey.c x_sig.c x_spki.c x_val.c )

add_submodule ( ${openssl_crypto_source_dir}/async async.c async_err.c async_wait.c
  arch/async_null.c arch/async_posix.c arch/async_win.c )

add_submodule ( ${openssl_crypto_source_dir}/bf bf_cfb64.c bf_ecb.c bf_enc.c bf_ofb64.c bf_skey.c )

add_submodule ( ${openssl_crypto_source_dir}/bio b_addr.c b_dump.c b_print.c b_sock.c b_sock2.c bf_buff.c #bf_lbuf.c
  bf_nbio.c bf_null.c bio_cb.c bio_err.c bio_lib.c bio_meth.c bss_acpt.c bss_bio.c
  bss_conn.c bss_dgram.c bss_fd.c bss_file.c bss_log.c bss_mem.c bss_null.c bss_sock.c )

add_submodule ( ${openssl_crypto_source_dir}/blake2 blake2b.c blake2s.c m_blake2b.c m_blake2s.c )

add_submodule ( ${openssl_crypto_source_dir}/bn bn_add.c bn_asm.c bn_blind.c bn_const.c bn_ctx.c bn_depr.c bn_dh.c
  bn_div.c bn_err.c bn_exp.c bn_exp2.c bn_gcd.c bn_gf2m.c bn_intern.c bn_kron.c bn_lib.c
  bn_mod.c bn_mont.c bn_mpi.c bn_mul.c bn_nist.c bn_prime.c bn_print.c bn_rand.c bn_recp.c
  bn_shift.c bn_sqr.c bn_sqrt.c bn_srp.c bn_word.c bn_x931p.c rsaz_exp.c rsa_sup_mul.c )

add_submodule ( ${openssl_crypto_source_dir}/buffer buf_err.c buffer.c )

add_submodule ( ${openssl_crypto_source_dir}/camellia camellia.c cmll_cbc.c cmll_cfb.c cmll_ctr.c cmll_ecb.c
  cmll_misc.c cmll_ofb.c )

add_submodule ( ${openssl_crypto_source_dir}/cast c_cfb64.c c_ecb.c c_enc.c c_ofb64.c c_skey.c )

add_submodule ( ${openssl_crypto_source_dir}/chacha chacha_enc.c )

add_submodule ( ${openssl_crypto_source_dir}/cmac cm_ameth.c cm_pmeth.c cmac.c )

add_submodule ( ${openssl_crypto_source_dir}/cms cms_asn1.c cms_att.c cms_cd.c cms_dd.c cms_enc.c cms_env.c cms_err.c
  cms_ess.c cms_io.c cms_kari.c cms_lib.c cms_pwri.c cms_sd.c cms_smime.c )

add_submodule ( ${openssl_crypto_source_dir}/comp c_zlib.c comp_err.c comp_lib.c )

add_submodule ( ${openssl_crypto_source_dir}/conf conf_api.c conf_def.c conf_err.c conf_lib.c conf_mall.c conf_mod.c
  conf_sap.c conf_ssl.c )

add_submodule ( ${openssl_crypto_source_dir}/ct ct_b64.c ct_err.c ct_log.c ct_oct.c ct_policy.c ct_prn.c ct_sct.c
  ct_sct_ctx.c ct_vfy.c ct_x509v3.c )

add_submodule ( ${openssl_crypto_source_dir}/des cbc_cksm.c cbc_enc.c cfb64ede.c cfb64enc.c cfb_enc.c des_enc.c
  ecb3_enc.c ecb_enc.c fcrypt.c fcrypt_b.c ofb64ede.c ofb64enc.c ofb_enc.c pcbc_enc.c
  qud_cksm.c rand_key.c set_key.c str2key.c xcbc_enc.c )

add_submodule ( ${openssl_crypto_source_dir}/dh dh_ameth.c dh_asn1.c dh_check.c dh_depr.c dh_err.c dh_gen.c dh_kdf.c
  dh_key.c dh_lib.c dh_meth.c dh_pmeth.c dh_prn.c dh_rfc5114.c dh_rfc7919.c )

add_submodule ( ${openssl_crypto_source_dir}/dsa dsa_ameth.c dsa_asn1.c dsa_depr.c dsa_err.c dsa_gen.c dsa_key.c
  dsa_lib.c dsa_meth.c dsa_ossl.c dsa_pmeth.c dsa_prn.c dsa_sign.c dsa_vrf.c )

add_submodule ( ${openssl_crypto_source_dir}/dso dso_dl.c dso_dlfcn.c dso_err.c dso_lib.c dso_openssl.c dso_vms.c
  dso_win32.c )

add_submodule ( ${openssl_crypto_source_dir}/ec curve25519.c ec2_oct.c ec2_smpl.c ec_ameth.c ec_asn1.c
  ec_check.c ec_curve.c ec_cvt.c ec_err.c ec_key.c ec_kmeth.c ec_lib.c ec_mult.c
  ec_oct.c ec_pmeth.c ec_print.c ecdh_kdf.c ecdh_ossl.c ecdsa_ossl.c ecdsa_sign.c
  ecdsa_vrf.c eck_prn.c ecp_mont.c ecp_nist.c ecp_nistp224.c ecp_nistp256.c ecp_nistp521.c
  ecp_nistputil.c ecp_oct.c ecp_smpl.c ecx_meth.c
  curve448/curve448.c curve448/curve448_tables.c curve448/eddsa.c curve448/f_generic.c
  curve448/scalar.c curve448/arch_32/f_impl.c )

add_submodule ( ${openssl_crypto_source_dir}/engine eng_all.c eng_cnf.c eng_ctrl.c eng_dyn.c eng_err.c
  eng_fat.c eng_init.c eng_lib.c eng_list.c eng_openssl.c eng_pkey.c eng_rdrand.c
  eng_table.c tb_asnmth.c tb_cipher.c tb_dh.c tb_digest.c tb_dsa.c tb_eckey.c tb_pkmeth.c
  tb_rand.c tb_rsa.c )

add_submodule ( ${openssl_crypto_source_dir}/err err.c err_all.c err_prn.c )

add_submodule ( ${openssl_crypto_source_dir}/evp bio_b64.c bio_enc.c bio_md.c bio_ok.c c_allc.c c_alld.c cmeth_lib.c
  digest.c e_aes.c e_aes_cbc_hmac_sha1.c e_aes_cbc_hmac_sha256.c e_aria.c e_bf.c e_camellia.c
  e_cast.c e_chacha20_poly1305.c e_des.c e_des3.c e_idea.c e_null.c e_old.c e_rc2.c
  e_rc4.c e_rc4_hmac_md5.c e_rc5.c e_sm4.c e_seed.c e_xcbc_d.c encode.c evp_cnf.c evp_enc.c
  evp_err.c evp_key.c evp_lib.c evp_pbe.c evp_pkey.c m_md2.c m_md4.c m_md5.c m_md5_sha1.c
  m_sha3.c m_mdc2.c m_null.c m_ripemd.c m_sha1.c m_sigver.c m_wp.c names.c p5_crpt.c p5_crpt2.c
  p_dec.c p_enc.c p_lib.c p_open.c p_seal.c p_sign.c p_verify.c pbe_scrypt.c
  pmeth_fn.c pmeth_gn.c pmeth_lib.c )

add_submodule ( ${openssl_crypto_source_dir}/hmac hm_ameth.c hm_pmeth.c hmac.c )

add_submodule ( ${openssl_crypto_source_dir}/idea i_cbc.c i_cfb64.c i_ecb.c i_ofb64.c i_skey.c )

add_submodule ( ${openssl_crypto_source_dir}/kdf hkdf.c kdf_err.c scrypt.c tls1_prf.c )

add_submodule ( ${openssl_crypto_source_dir}/lhash lh_stats.c lhash.c )

add_submodule ( ${openssl_crypto_source_dir}/md4 md4_dgst.c md4_one.c )

add_submodule ( ${openssl_crypto_source_dir}/md5 md5_dgst.c md5_one.c )

add_submodule ( ${openssl_crypto_source_dir}/mdc2 mdc2_one.c mdc2dgst.c )

add_submodule ( ${openssl_crypto_source_dir}/modes cbc128.c ccm128.c cfb128.c ctr128.c cts128.c gcm128.c ocb128.c
  ofb128.c wrap128.c xts128.c )

add_submodule ( ${openssl_crypto_source_dir}/objects o_names.c obj_dat.c obj_err.c obj_lib.c obj_xref.c )

add_submodule ( ${openssl_crypto_source_dir}/ocsp ocsp_asn.c ocsp_cl.c ocsp_err.c ocsp_ext.c ocsp_ht.c ocsp_lib.c
  ocsp_prn.c ocsp_srv.c ocsp_vfy.c v3_ocsp.c )

add_submodule ( ${openssl_crypto_source_dir}/pem pem_all.c pem_err.c pem_info.c pem_lib.c pem_oth.c pem_pk8.c pem_pkey.c
  pem_sign.c pem_x509.c pem_xaux.c pvkfmt.c )

add_submodule ( ${openssl_crypto_source_dir}/pkcs12 p12_add.c p12_asn.c p12_attr.c p12_crpt.c p12_crt.c p12_decr.c
  p12_init.c p12_key.c p12_kiss.c p12_mutl.c p12_npas.c p12_p8d.c p12_p8e.c p12_sbag.c
  p12_utl.c pk12err.c )

add_submodule ( ${openssl_crypto_source_dir}/pkcs7 bio_pk7.c pk7_asn1.c pk7_attr.c pk7_doit.c pk7_lib.c pk7_mime.c
  pk7_smime.c pkcs7err.c )

add_submodule ( ${openssl_crypto_source_dir}/poly1305 poly1305.c poly1305_ameth.c poly1305_pmeth.c)

add_submodule ( ${openssl_crypto_source_dir}/rand drbg_ctr.c drbg_lib.c rand_egd.c rand_err.c rand_lib.c rand_unix.c rand_vms.c
 rand_win.c randfile.c )

add_submodule ( ${openssl_crypto_source_dir}/rc2 rc2_cbc.c rc2_ecb.c rc2_skey.c rc2cfb64.c rc2ofb64.c )

add_submodule ( ${openssl_crypto_source_dir}/rc4 rc4_enc.c rc4_skey.c )

#add_submodule ( ${openssl_crypto_source_dir}/rc5 rc5_ecb.c rc5_enc.c rc5_skey.c rc5cfb64.c rc5ofb64.c )

add_submodule ( ${openssl_crypto_source_dir}/ripemd rmd_dgst.c rmd_one.c )

add_submodule ( ${openssl_crypto_source_dir}/rsa rsa_ameth.c rsa_asn1.c rsa_chk.c rsa_crpt.c rsa_depr.c rsa_err.c
  rsa_gen.c rsa_lib.c rsa_meth.c rsa_mp.c rsa_none.c rsa_oaep.c rsa_ossl.c rsa_pk1.c
  rsa_pmeth.c rsa_prn.c rsa_pss.c rsa_saos.c rsa_sign.c rsa_ssl.c rsa_x931.c rsa_x931g.c )

add_submodule ( ${openssl_crypto_source_dir}/seed seed.c seed_cbc.c seed_cfb.c seed_ecb.c seed_ofb.c )

add_submodule ( ${openssl_crypto_source_dir}/sha keccak1600.c sha1_one.c sha1dgst.c sha256.c sha512.c )

add_submodule ( ${openssl_crypto_source_dir}/siphash siphash.c siphash_ameth.c siphash_pmeth.c )

add_submodule ( ${openssl_crypto_source_dir}/sm2 sm2_crypt.c sm2_err.c sm2_pmeth.c sm2_sign.c )

add_submodule ( ${openssl_crypto_source_dir}/sm3 m_sm3.c sm3.c )

add_submodule ( ${openssl_crypto_source_dir}/sm4 sm4.c )

add_submodule ( ${openssl_crypto_source_dir}/srp srp_lib.c srp_vfy.c )

add_submodule ( ${openssl_crypto_source_dir}/stack stack.c )

add_submodule ( ${openssl_crypto_source_dir}/store loader_file.c store_err.c store_init.c store_lib.c store_register.c store_strings.c )

add_submodule ( ${openssl_crypto_source_dir}/ts ts_asn1.c ts_conf.c ts_err.c ts_lib.c ts_req_print.c ts_req_utils.c
  ts_rsp_print.c ts_rsp_sign.c ts_rsp_utils.c ts_rsp_verify.c ts_verify_ctx.c )

add_submodule ( ${openssl_crypto_source_dir}/txt_db txt_db.c )

add_submodule ( ${openssl_crypto_source_dir}/ui ui_err.c ui_lib.c ui_null.c ui_openssl.c ui_util.c )

add_submodule ( ${openssl_crypto_source_dir}/whrlpool wp_block.c wp_dgst.c )

add_submodule ( ${openssl_crypto_source_dir}/x509 by_dir.c by_file.c t_crl.c t_req.c t_x509.c x509_att.c x509_cmp.c
  x509_d2.c x509_def.c x509_err.c x509_ext.c x509_lu.c x509_meth.c x509_obj.c x509_r2x.c
  x509_req.c x509_set.c x509_trs.c x509_txt.c x509_v3.c x509_vfy.c x509_vpm.c x509cset.c
  x509name.c x509rset.c x509spki.c x509type.c x_all.c x_attrib.c x_crl.c x_exten.c x_name.c
  x_pubkey.c x_req.c x_x509.c x_x509a.c )

add_submodule ( ${openssl_crypto_source_dir}/x509v3 pcy_cache.c pcy_data.c pcy_lib.c pcy_map.c pcy_node.c pcy_tree.c
  v3_addr.c v3_admis.c v3_akey.c v3_akeya.c v3_alt.c v3_asid.c v3_bcons.c v3_bitst.c v3_conf.c v3_cpols.c
  v3_crld.c v3_enum.c v3_extku.c v3_genn.c v3_ia5.c v3_info.c v3_int.c v3_lib.c v3_ncons.c
  v3_pci.c v3_pcia.c v3_pcons.c v3_pku.c v3_pmaps.c v3_prn.c v3_purp.c v3_skey.c v3_sxnet.c
  v3_tlsf.c v3_utl.c v3err.c )

set( CMAKE_THREAD_PREFER_PTHREAD TRUE )
find_package ( Threads )
if( NOT Threads_FOUND )
  add_submodule ( ${openssl_crypto_source_dir} threads_none.c )
elseif( WIN32 )
  add_submodule ( ${openssl_crypto_source_dir} threads_win.c )
else()
  add_submodule ( ${openssl_crypto_source_dir} threads_pthread.c )
endif()

#### OpenSSL Crypto | Library Source / Includes / Definitions / Options
add_library(openssl_crypto STATIC ${openssl_crypto_source_files})

target_include_directories(openssl_crypto SYSTEM BEFORE PRIVATE
    ${openssl_crypto_include_dir})


if( WIN32 AND NOT CYGWIN )
  target_link_libraries( openssl_crypto ws2_32 crypt32 )
else()
  if( Threads_FOUND )
    target_link_libraries( openssl_crypto ${CMAKE_THREAD_LIBS_INIT} )
  endif()
  if( DSO_DLFCN AND HAVE_DLFCN_H )
    target_link_libraries( openssl_crypto dl )
  endif()
endif()

if( BUILD_SHARED_LIBS )
    target_compile_definitions(openssl_crypto PUBLIC OPENSSL_BUILD_SHLIBCRYPTO )
endif()

set_target_properties( openssl_crypto PROPERTIES
  VERSION "${LIB_VERSION}" SOVERSION "${LIB_SOVERSION}" )

if( ANDROID )
  set_target_properties( openssl_crypto PROPERTIES OUTPUT_NAME "crypto_1_1" )
endif()

if( MSVC )
  if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
    set_target_properties( openssl_crypto PROPERTIES OUTPUT_NAME "crypto-1_1-x64" )
  elseif( CMAKE_SIZEOF_VOID_P EQUAL 4 )
    set_target_properties( openssl_crypto PROPERTIES OUTPUT_NAME "crypto-1_1" )
  endif()
  
endif()

### OpenSSL SSL

set(openssl_ssl_source_dir ${openssl_source_dir}/ssl)

#### OpenSSL SSL | Collect Source files

set( SOURCE_LIST
  bio_ssl.c d1_lib.c d1_msg.c d1_srtp.c methods.c packet.c pqueue.c s3_cbc.c s3_enc.c s3_lib.c
  s3_msg.c ssl_asn1.c ssl_cert.c ssl_ciph.c ssl_conf.c ssl_err.c ssl_init.c ssl_lib.c
  ssl_mcnf.c ssl_rsa.c ssl_sess.c ssl_stat.c ssl_txt.c ssl_utst.c t1_enc.c t1_lib.c
  t1_trce.c tls13_enc.c tls_srp.c
  record/dtls1_bitmap.c record/rec_layer_d1.c record/rec_layer_s3.c record/ssl3_buffer.c
  record/ssl3_record.c record/ssl3_record_tls13.c
  statem/extensions.c statem/extensions_clnt.c statem/extensions_cust.c
  statem/extensions_srvr.c statem/statem.c statem/statem_clnt.c statem/statem_dtls.c
  statem/statem_lib.c statem/statem_srvr.c
  )

set(openssl_ssl_source_files "")
foreach(element ${SOURCE_LIST})
    list(APPEND openssl_ssl_source_files "${openssl_ssl_source_dir}/${element}")
endforeach()

add_library(openssl_ssl STATIC ${openssl_ssl_source_files})

target_include_directories(openssl_ssl PRIVATE ${openssl_source_dir} ${openssl_include_dir})
add_dependencies(openssl_ssl openssl_crypto)
target_link_libraries(openssl_ssl openssl_crypto)

if( BUILD_SHARED_LIBS )
  target_compile_definitions(openssl_ssl PUBLIC OPENSSL_BUILD_SHLIBSSL)
endif()

set_target_properties( openssl_ssl PROPERTIES
  VERSION "${LIB_VERSION}" SOVERSION "${LIB_SOVERSION}" )

if( ANDROID )
  set_target_properties( openssl_ssl PROPERTIES OUTPUT_NAME "ssl_1_1" )
endif()

if( MSVC )
  if( CMAKE_SIZEOF_VOID_P EQUAL 8 )
    set_target_properties( openssl_ssl PROPERTIES OUTPUT_NAME "ssl-1_1-x64" )
  elseif( CMAKE_SIZEOF_VOID_P EQUAL 4 )
    set_target_properties( openssl_ssl PROPERTIES OUTPUT_NAME "ssl-1_1" )
  endif()  
endif()

if (ANDROID)
set_target_properties(openssl_ssl PROPERTIES LIBRARY_OUTPUT_DIRECTORY
    ${openssl_binary_dir}/${ANDROID_ABI})
set_target_properties(openssl_ssl PROPERTIES ARCHIVE_OUTPUT_DIRECTORY
    ${openssl_binary_dir}/${ANDROID_ABI})
set_target_properties(openssl_ssl PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    ${openssl_binary_dir}/${ANDROID_ABI})
else()
set_target_properties(openssl_ssl PROPERTIES LIBRARY_OUTPUT_DIRECTORY
    ${openssl_binary_dir})
set_target_properties(openssl_ssl PROPERTIES ARCHIVE_OUTPUT_DIRECTORY
    ${openssl_binary_dir})
set_target_properties(openssl_ssl PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    ${openssl_binary_dir})
endif()

## Security Socket

message("-- Security Socket CMake Initialization --")

set(securitysocket_build_dir ${build_dir}/SecuritySocket)
set(securitysocket_include_dir ${securitysocket_build_dir}/include)
set(securitysocket_binary_dir ${securitysocket_build_dir}/lib)

file(GLOB_RECURSE securitysocket_source_files 
    ${source_dir}/*.h
    ${source_dir}/*.hpp
    ${source_dir}/*.c
    ${source_dir}/*.cpp)
message("securitysocket_source_files = ${securitysocket_source_files}")

add_library(securitysocket STATIC ${securitysocket_source_files})
target_include_directories(
    securitysocket PRIVATE "${openssl_include_dir}"
)
add_dependencies(securitysocket openssl_crypto openssl_ssl)
# target_link_libraries(securitysocket PUBLIC openssl_crypto openssl_ssl)
target_sources(securitysocket PRIVATE $<TARGET_OBJECTS:openssl_crypto> $<TARGET_OBJECTS:openssl_ssl>)

configure_file(${source_dir}/SecuritySocket.hpp ${securitysocket_include_dir}/SecuritySocket.hpp)

if (ANDROID)
set_target_properties(securitysocket PROPERTIES LIBRARY_OUTPUT_DIRECTORY
    ${securitysocket_binary_dir}/${ANDROID_ABI})
set_target_properties(securitysocket PROPERTIES ARCHIVE_OUTPUT_DIRECTORY
    ${securitysocket_binary_dir}/${ANDROID_ABI})
set_target_properties(securitysocket PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    ${securitysocket_binary_dir}/${ANDROID_ABI})
else()
set_target_properties(securitysocket PROPERTIES LIBRARY_OUTPUT_DIRECTORY
    ${securitysocket_binary_dir})
set_target_properties(securitysocket PROPERTIES ARCHIVE_OUTPUT_DIRECTORY
    ${securitysocket_binary_dir})
set_target_properties(securitysocket PROPERTIES RUNTIME_OUTPUT_DIRECTORY
    ${securitysocket_binary_dir})
endif()

## Security Socket Test App

message("-- Security Socket Sample App CMake Initialization --")

add_library(securitysocket_test SHARED securitysocket.cpp)
add_dependencies(securitysocket_test securitysocket)
find_library(log-lib log)
find_library(android-lib android)
target_link_libraries(securitysocket_test securitysocket ${log-lib} ${android-lib})
target_include_directories(securitysocket_test PRIVATE ${securitysocket_include_dir})

