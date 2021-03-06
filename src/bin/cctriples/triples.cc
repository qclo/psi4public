/*
 *@BEGIN LICENSE
 *
 * PSI4: an ab initio quantum chemistry software package
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *@END LICENSE
 */

/*! \file
    \ingroup CCTRIPLES
    \brief Enter brief description of file here
*/
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <libciomr/libciomr.h>
#include <libpsio/psio.h>
#include <libchkpt/chkpt.h>
#include <libdpd/dpd.h>
#include <libqt/qt.h>
#include <psifiles.h>
#include <psi4-dec.h>
#include "Params.h"
#include "MOInfo.h"
#include "globals.h"

namespace psi { namespace cctriples {


    void init_io();
    void title(void);
    void get_moinfo(Options&);
    void exit_io(void);
    void cleanup(void);
    double ET_RHF(void);
    double EaT_RHF(void);
    double ET_AAA(void);
    double ET_AAB(void);
    double ET_ABB(void);
    double ET_BBB(void);
    double ET_UHF_AAA(void);
    double ET_UHF_BBB(void);
    double ET_UHF_AAB(void);
    double ET_UHF_ABB(void);
    void count_ijk(void);
    void setup(void);
    int **cacheprep_rhf(int level, int *cachefiles);
    int **cacheprep_uhf(int level, int *cachefiles);
    void cachedone_uhf(int **cachelist);
    void cachedone_rhf(int **cachelist);

    void T3_grad_RHF(void);
    double T3_grad_UHF_AAA(void);
    double T3_grad_UHF_BBB(void);
    double T3_grad_UHF_AAB(void);
    double T3_grad_UHF_BBA(void);


    void T3_UHF_AAA_abc(double ***W, double ***V, int disc, int nirreps, int A, int Ga, int B, int Gb, int C, int Gc,
            dpdbuf4 *C2, dpdbuf4 *F, dpdbuf4 *E, dpdfile2 *C1, dpdbuf4 *D, dpdfile2 *fIA, dpdfile2 *fIJ, dpdfile2 *fAB,
            int *occpi, int *occ_off, int *virtpi, int *vir_off, double omega);

    void T3_UHF_AAB_abc(double ***W, double ***V, int disc, int nirreps,
            int I, int Gi, int J, int Gj, int K, int Gk,
            dpdbuf4 *T2AA, dpdbuf4 *T2AB, dpdbuf4 *T2BA, dpdbuf4 *FAA, dpdbuf4 *FAB, dpdbuf4 *FBA,
            dpdbuf4 *EAA, dpdbuf4 *EAB, dpdbuf4 *EBA, dpdfile2 *T1A, dpdfile2 *T1B,
            dpdbuf4 *DAA, dpdbuf4 *DAB, dpdfile2 *fIA, dpdfile2 *fia,
            dpdfile2 *fIJ, dpdfile2 *fij,dpdfile2 *fAB, dpdfile2 *fab,
            int *aoccpi, int *aocc_off, int *boccpi, int *bocc_off,
            int *avirtpi, int *avir_off, int *bvirtpi, int *bvir_off, double omega);
    void transpose_integrals();
    void test_abc_loops_AAA();
    void test_abc_loops_AAB();
    void test_abc_loops_BBA();
    void test_abc_loops_BBB();

PsiReturnType cctriples(Options &options)
{
  double ETAAA, ETAAB, ETABB, ETBBB, ET;
  long int memory;
  int **cachelist, *cachefiles;
  dpdfile2 T1;
  double **geom, *zvals, value;
  FILE *efile;
  int i, errcod, natom;
  char *keyw = NULL;

  init_io();
  title();

  timer_on("CCtriples");

  get_moinfo(options);
  memory = Process::environment.get_memory();

  cachefiles = init_int_array(PSIO_MAXUNIT);


  if(params.ref == 0) { /*** RHF ***/
    cachelist = cacheprep_rhf(2, cachefiles);

    std::vector<int*> spaces;
    spaces.push_back(moinfo.occpi);
    spaces.push_back(moinfo.occ_sym);
    spaces.push_back(moinfo.virtpi);
    spaces.push_back(moinfo.vir_sym);
    dpd_init(0, moinfo.nirreps, memory, 0, cachefiles, cachelist, NULL, 2, spaces);
  }
  else if(params.ref == 2) { /*** UHF ***/
    cachelist = cacheprep_uhf(2, cachefiles);

    std::vector<int*> spaces;
    spaces.push_back(moinfo.aoccpi);
    spaces.push_back(moinfo.aocc_sym);
    spaces.push_back(moinfo.avirtpi);
    spaces.push_back(moinfo.avir_sym);
    spaces.push_back(moinfo.boccpi);
    spaces.push_back(moinfo.bocc_sym);
    spaces.push_back(moinfo.bvirtpi);
    spaces.push_back(moinfo.bvir_sym);

    dpd_init(0, moinfo.nirreps, memory, 0, cachefiles, cachelist, NULL, 4, spaces);
  }

  count_ijk();
  fflush(outfile);

  if(params.ref == 0) { /** RHF **/

    if(params.wfn=="CCSD_T" || params.wfn=="BCCD_T") {
      ET = ET_RHF();
      fprintf(outfile, "\t(T) energy                    = %20.15f\n", ET);
      fprintf(outfile, "      * CCSD(T) total energy          = %20.15f\n",
          ET + moinfo.ecc + moinfo.eref);

      Process::environment.globals["(T) CORRECTION ENERGY"] = ET;
      Process::environment.globals["CCSD(T) CORRELATION ENERGY"] = ET + moinfo.ecc;
      Process::environment.globals["CCSD(T) TOTAL ENERGY"] = ET + moinfo.ecc + moinfo.eref;
    }
    else if(params.wfn=="CCSD_AT") {
      ET = EaT_RHF();
      fprintf(outfile, "\t(aT) energy                    = %20.15f\n", ET);
      fprintf(outfile, "      * CCSD(aT) total energy          = %20.15f\n",
          ET + moinfo.ecc + moinfo.eref);

      Process::environment.globals["A-(T) CORRECTION ENERGY"] = ET;
      Process::environment.globals["A-CCSD(T) CORRELATION ENERGY"] = ET + moinfo.ecc;
      Process::environment.globals["A-CCSD(T) TOTAL ENERGY"] = ET + moinfo.ecc + moinfo.eref;
    }

    /* Compute triples contributions to the gradient */
    if(params.dertype == 1){
      T3_grad_RHF();
    }
  }
  else if(params.ref == 1 ) { /** ROHF --- don't use this right now! **/

    throw PsiException("ROHF-CCSD(T) is not yet available",__FILE__,__LINE__);

    ETAAA = ET_AAA();
    fprintf(outfile, "\tAAA (T) energy                = %20.15f\n", ETAAA);
    ETAAB = ET_AAB();
    fprintf(outfile, "\tAAB (T) energy                = %20.15f\n", ETAAB);
    ETABB = ET_ABB();
    fprintf(outfile, "\tABB (T) energy                = %20.15f\n", ETABB);
    ETBBB = ET_BBB();
    fprintf(outfile, "\tBBB (T) energy                = %20.15f\n", ETBBB);
    ET = ETAAA + ETAAB + ETABB + ETBBB;
    fprintf(outfile, "\t(T) energy                    = %20.15f\n", ET);
    fprintf(outfile, "      * CCSD(T) total energy          = %20.15f\n",
        ET + moinfo.ecc + moinfo.eref);

    Process::environment.globals["AAA (T) CORRECTION ENERGY"] = ETAAA;
    Process::environment.globals["AAB (T) CORRECTION ENERGY"] = ETAAB;
    Process::environment.globals["ABB (T) CORRECTION ENERGY"] = ETABB;
    Process::environment.globals["BBB (T) CORRECTION ENERGY"] = ETBBB;
    Process::environment.globals["(T) CORRECTION ENERGY"] = ET;
    Process::environment.globals["CCSD(T) CORRELATION ENERGY"] = ET + moinfo.ecc;
    Process::environment.globals["CCSD(T) TOTAL ENERGY"] = ET + moinfo.ecc + moinfo.eref;
  }
  else if(params.ref == 2) { /** UHF **/

    if(params.dertype == 0) {
      ETAAA = ET_UHF_AAA();
      fprintf(outfile, "\tAAA (T) energy                = %20.15f\n", ETAAA);
      fflush(outfile);

      ETBBB = ET_UHF_BBB();
      fprintf(outfile, "\tBBB (T) energy                = %20.15f\n", ETBBB);
      fflush(outfile);

      ETAAB = ET_UHF_AAB();
      fprintf(outfile, "\tAAB (T) energy                = %20.15f\n", ETAAB);
      fflush(outfile);

      ETABB = ET_UHF_ABB();
      fprintf(outfile, "\tABB (T) energy                = %20.15f\n", ETABB);
      fflush(outfile);
    }
    else if(params.dertype==1) {
      transpose_integrals();
      fprintf(outfile, "\n\tComputing (T) contributions to CC density...\n");
      fflush(outfile);

      ETAAA = T3_grad_UHF_AAA();
      fprintf(outfile, "\tAAA (T) energy                = %20.15f\n", ETAAA);
      fflush(outfile);

      ETBBB = T3_grad_UHF_BBB();
      fprintf(outfile, "\tBBB (T) energy                = %20.15f\n", ETBBB);
      fflush(outfile);

      ETAAB = T3_grad_UHF_AAB();
      fprintf(outfile, "\tAAB (T) energy                = %20.15f\n", ETAAB);
      fflush(outfile);

      ETABB = T3_grad_UHF_BBA();
      fprintf(outfile, "\tABB (T) energy                = %20.15f\n", ETABB);
      fflush(outfile);
    }

    ET = ETAAA + ETAAB + ETABB + ETBBB;
    fprintf(outfile, "\t(T) energy                    = %20.15f\n", ET);
    fprintf(outfile, "      * CCSD(T) total energy          = %20.15f\n",
        ET + moinfo.ecc + moinfo.eref);

    Process::environment.globals["AAA (T) CORRECTION ENERGY"] = ETAAA;
    Process::environment.globals["AAB (T) CORRECTION ENERGY"] = ETAAB;
    Process::environment.globals["ABB (T) CORRECTION ENERGY"] = ETABB;
    Process::environment.globals["BBB (T) CORRECTION ENERGY"] = ETBBB;
    Process::environment.globals["(T) CORRECTION ENERGY"] = ET;
    Process::environment.globals["CCSD(T) CORRELATION ENERGY"] = ET + moinfo.ecc;
    Process::environment.globals["CCSD(T) TOTAL ENERGY"] = ET + moinfo.ecc + moinfo.eref;

  } // UHF

  fprintf(outfile, "\n");

  /* Write total energy and (T) contribution to the checkpoint file */
  chkpt_init(PSIO_OPEN_OLD);
  chkpt_wt_etot(ET+moinfo.ecc+moinfo.eref);
  chkpt_wt_e_t(ET);
  chkpt_close();

  /* Dump triples energy to CC_INFO and the python environment*/
  psio_write_entry(PSIF_CC_INFO, "(T) Energy", (char *) &(ET), sizeof(double));

  Process::environment.globals["CURRENT ENERGY"] = ET+ moinfo.ecc+moinfo.eref;
  Process::environment.globals["CURRENT CORRELATION ENERGY"] = ET+ moinfo.ecc;

  dpd_close(0);

  if(params.ref == 2) cachedone_uhf(cachelist);
  else cachedone_rhf(cachelist);
  free(cachefiles);

  cleanup();

  timer_off("CCtriples");

  exit_io();
  return Success;
}

void init_io()
{
  tstart();

  for(int i=PSIF_CC_MIN; i <= PSIF_CC_MAX; i++) psio_open(i,1);
}

void title(void)
{
  fprintf(outfile, "\t\t\t**************************\n");
  fprintf(outfile, "\t\t\t*                        *\n");
  fprintf(outfile, "\t\t\t*        CCTRIPLES       *\n");
  fprintf(outfile, "\t\t\t*                        *\n");
  fprintf(outfile, "\t\t\t**************************\n");
}

void exit_io(void)
{
  int i;
  for(i=PSIF_CC_MIN; i <= PSIF_CC_MAX; i++) psio_close(i,1);
  tstop();
}


}} // namespace psi::CCTRIPLES
