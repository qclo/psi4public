set(SRC build_A_RHF.cc build_A_ROHF.cc build_A_UHF.cc cache.cc diag_A_RHF.cc diag_A_ROHF.cc diag_A_UHF.cc follow_evec_UHF.cc get_moinfo.cc get_params.cc print_evals.cc stable.cc)
add_library(stable ${SRC})
add_dependencies(stable mints)
