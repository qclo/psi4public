set(SRC b2brepl.cc calc_d.cc calc_hd_block.cc check_energy.cc civect.cc compute_cc.cc detci.cc form_ov.cc get_mo_info.cc h0block.cc import_vector.cc ints.cc misc.cc mitrush_iter.cc mpn.cc odometer.cc og_addr.cc olsengraph.cc olsenupdt.cc opdm.cc params.cc printing.cc s1.cc s1v.cc s2.cc s2v.cc s3.cc s3_block_bz.cc s3v.cc sem.cc sem_test.cc set_ciblks.cc shift.cc sigma.cc slater.cc slaterd.cc ssq.cc stringlist.cc time.cc tpdm.cc tpool.cc vector.cc)
add_library(detci ${SRC})
add_dependencies(detci mints)
