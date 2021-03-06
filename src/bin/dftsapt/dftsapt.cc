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

#include "dftsapt.h"
#include <libmints/mints.h>
#include <libmints/sieve.h>
#include <libmints/local.h>
#include <libfock/jk.h>
#include <libthce/thce.h>
#include <libthce/lreri.h>
#include <libqt/qt.h>
#include <libpsio/psio.hpp>
#include <libpsio/psio.h>
#include <psi4-dec.h>
#include <psifiles.h>
#include <physconst.h>

#ifdef _OPENMP
#include <omp.h>
#endif

using namespace psi;
using namespace boost;
using namespace std;

namespace psi {
namespace dftsapt {

DFTSAPT::DFTSAPT()
{
    common_init();
}
DFTSAPT::~DFTSAPT()
{
}
void DFTSAPT::common_init()
{
    print_ = 1;
    debug_ = 0;
    bench_ = 0;
}
boost::shared_ptr<DFTSAPT> DFTSAPT::build(boost::shared_ptr<Wavefunction> d,
                                          boost::shared_ptr<Wavefunction> mA,
                                          boost::shared_ptr<Wavefunction> mB)
{
    Options& options = Process::environment.options;

    DFTSAPT* sapt;
    if (options.get_str("DFT_SAPT_TYPE") == "DFT-SAPT") {
        sapt = new DFTSAPT();
    } else if (options.get_str("DFT_SAPT_TYPE") == "SAPT0") {
        sapt = new DFTSAPT();
    } else {
        throw PSIEXCEPTION("Invalid DFT_SAPT_TYPE");
    }

    sapt->type_  = options.get_str("DFT_SAPT_TYPE");

    sapt->print_ = options.get_int("PRINT");
    sapt->debug_ = options.get_int("DEBUG");
    sapt->bench_ = options.get_int("BENCH");

    sapt->memory_ = (unsigned long int)(Process::environment.get_memory() * options.get_double("SAPT_MEM_FACTOR") * 0.125);

    sapt->cpks_maxiter_ = options.get_int("MAXITER");
    sapt->cpks_delta_ = options.get_double("D_CONVERGENCE");

    sapt->freq_points_ = options.get_int("FREQ_POINTS");
    sapt->freq_scale_  = options.get_double("FREQ_SCALE");
    sapt->freq_max_k_  = options.get_int("FREQ_MAX_K");

    sapt->dimer_     = d->molecule();
    sapt->monomer_A_ = mA->molecule();
    sapt->monomer_B_ = mB->molecule();

    sapt->E_dimer_     = d->reference_energy();
    sapt->E_monomer_A_ = mA->reference_energy();
    sapt->E_monomer_B_ = mB->reference_energy();

    sapt->primary_   = d->basisset();
    sapt->primary_A_ = mA->basisset();
    sapt->primary_B_ = mB->basisset();

    if (sapt->primary_A_->nbf() != sapt->primary_B_->nbf() || sapt->primary_->nbf() != sapt->primary_A_->nbf()) {
        throw PSIEXCEPTION("Monomer-centered bases not allowed in DFT-SAPT");
    }

    sapt->Cocc_A_     = mA->Ca_subset("AO","OCC");
    sapt->Cvir_A_     = mA->Ca_subset("AO","VIR");
    sapt->eps_occ_A_  = mA->epsilon_a_subset("AO","OCC");
    sapt->eps_vir_A_  = mA->epsilon_a_subset("AO","VIR");

    sapt->Cfocc_A_    = mA->Ca_subset("AO","FROZEN_OCC");
    sapt->Caocc_A_    = mA->Ca_subset("AO","ACTIVE_OCC");
    sapt->Cavir_A_    = mA->Ca_subset("AO","ACTIVE_VIR");
    sapt->Cfvir_A_    = mA->Ca_subset("AO","FROZEN_VIR");

    sapt->eps_focc_A_ = mA->epsilon_a_subset("AO","FROZEN_OCC");
    sapt->eps_aocc_A_ = mA->epsilon_a_subset("AO","ACTIVE_OCC");
    sapt->eps_avir_A_ = mA->epsilon_a_subset("AO","ACTIVE_VIR");
    sapt->eps_fvir_A_ = mA->epsilon_a_subset("AO","FROZEN_VIR");

    sapt->Cocc_B_     = mB->Ca_subset("AO","OCC");
    sapt->Cvir_B_     = mB->Ca_subset("AO","VIR");
    sapt->eps_occ_B_  = mB->epsilon_a_subset("AO","OCC");
    sapt->eps_vir_B_  = mB->epsilon_a_subset("AO","VIR");

    sapt->Cfocc_B_    = mB->Ca_subset("AO","FROZEN_OCC");
    sapt->Caocc_B_    = mB->Ca_subset("AO","ACTIVE_OCC");
    sapt->Cavir_B_    = mB->Ca_subset("AO","ACTIVE_VIR");
    sapt->Cfvir_B_    = mB->Ca_subset("AO","FROZEN_VIR");

    sapt->eps_focc_B_ = mB->epsilon_a_subset("AO","FROZEN_OCC");
    sapt->eps_aocc_B_ = mB->epsilon_a_subset("AO","ACTIVE_OCC");
    sapt->eps_avir_B_ = mB->epsilon_a_subset("AO","ACTIVE_VIR");
    sapt->eps_fvir_B_ = mB->epsilon_a_subset("AO","FROZEN_VIR");

    boost::shared_ptr<BasisSetParser> parser(new Gaussian94BasisSetParser());
    sapt->mp2fit_ = BasisSet::construct(parser, sapt->dimer_, "DF_BASIS_SAPT");

    return boost::shared_ptr<DFTSAPT>(sapt);
}
double DFTSAPT::compute_energy()
{
    energies_["HF"] = E_dimer_ - E_monomer_A_ - E_monomer_B_; // TODO: get dHF loaded correctly

    print_header();

    if (type_ == "SAPT0") {
        fock_terms();
        mp2_terms();
        print_trailer();
    } else if (type_ == "DFT-SAPT") {
        fock_terms();
        mp2_terms();
        tdhf_demo();
        print_trailer();
    } else {
        throw PSIEXCEPTION("DFTSAPT: Unrecognized type");
    }

    Process::environment.globals["SAPT ENERGY"] = 0.0;

    return 0.0;
}
void DFTSAPT::print_header() const
{
    fprintf(outfile, "\t --------------------------------------------------------\n");
    fprintf(outfile, "\t                        DF-DFT-SAPT                      \n");
    fprintf(outfile, "\t               Rob Parrish and Ed Hohenstein             \n");
    fprintf(outfile, "\t --------------------------------------------------------\n");
    fprintf(outfile, "\n");

    fprintf(outfile, "  ==> Sizes <==\n");
    fprintf(outfile, "\n");

    fprintf(outfile, "   => Resources <=\n\n");

    fprintf(outfile, "    Memory (MB):       %11ld\n", (memory_ *8L) / (1024L * 1024L));
    fprintf(outfile, "\n");

    fprintf(outfile, "   => Orbital Ranges <=\n\n");

    int nmo_A = eps_focc_A_->dim() + eps_aocc_A_->dim() + eps_avir_A_->dim() + eps_fvir_A_->dim();
    int nmo_B = eps_focc_B_->dim() + eps_aocc_B_->dim() + eps_avir_B_->dim() + eps_fvir_B_->dim();

    int nA = 0;
    for (int A = 0; A < monomer_A_->natom(); A++) {
        if (monomer_A_->Z(A) != 0.0) nA++;
    }

    int nB = 0;
    for (int B = 0; B < monomer_B_->natom(); B++) {
        if (monomer_B_->Z(B) != 0.0) nB++;
    }

    fprintf(outfile, "    ------------------\n");
    fprintf(outfile, "    %-6s %5s %5s\n", "Range", "M_A", "M_B");
    fprintf(outfile, "    ------------------\n");
    fprintf(outfile, "    %-6s %5d %5d\n", "natom", nA, nB);
    fprintf(outfile, "    %-6s %5d %5d\n", "nso", primary_A_->nbf(), primary_B_->nbf());
    fprintf(outfile, "    %-6s %5d %5d\n", "nmo", nmo_A, nmo_B);
    fprintf(outfile, "    %-6s %5d %5d\n", "nocc", eps_aocc_A_->dim() + eps_focc_A_->dim(), eps_aocc_B_->dim() + eps_focc_B_->dim());
    fprintf(outfile, "    %-6s %5d %5d\n", "nvir", eps_avir_A_->dim() + eps_fvir_A_->dim(), eps_avir_B_->dim() + eps_fvir_B_->dim());
    fprintf(outfile, "    %-6s %5d %5d\n", "nfocc", eps_focc_A_->dim(), eps_focc_B_->dim());
    fprintf(outfile, "    %-6s %5d %5d\n", "naocc", eps_aocc_A_->dim(), eps_aocc_B_->dim());
    fprintf(outfile, "    %-6s %5d %5d\n", "navir", eps_avir_A_->dim(), eps_avir_B_->dim());
    fprintf(outfile, "    %-6s %5d %5d\n", "nfvir", eps_fvir_A_->dim(), eps_fvir_B_->dim());
    fprintf(outfile, "    ------------------\n");
    fprintf(outfile, "\n");

    fprintf(outfile, "   => Primary Basis Set <=\n\n");
    primary_->print_by_level(outfile, print_);

    fflush(outfile);
}
void DFTSAPT::fock_terms()
{
    fprintf(outfile, "  SCF TERMS:\n\n");

    // ==> Setup <== //

    // => Compute the D matrices <= //

    boost::shared_ptr<Matrix> D_A    = Matrix::doublet(Cocc_A_, Cocc_A_,false,true);
    boost::shared_ptr<Matrix> D_B    = Matrix::doublet(Cocc_B_, Cocc_B_,false,true);

    // => Compute the P matrices <= //

    boost::shared_ptr<Matrix> P_A    = Matrix::doublet(Cvir_A_, Cvir_A_,false,true);
    boost::shared_ptr<Matrix> P_B    = Matrix::doublet(Cvir_B_, Cvir_B_,false,true);

    // => Compute the S matrix <= //

    boost::shared_ptr<Matrix> S = build_S(primary_);

    // => Compute the V matrices <= //

    boost::shared_ptr<Matrix> V_A = build_V(primary_A_);
    boost::shared_ptr<Matrix> V_B = build_V(primary_B_);

    // => JK Object <= //

    boost::shared_ptr<JK> jk = JK::build_JK();

    // TODO: Account for 2-index overhead in memory
    int nA = Cocc_A_->ncol();
    int nB = Cocc_B_->ncol();
    int nso = Cocc_A_->nrow();
    long int jk_memory = (long int)memory_;
    jk_memory -= 24 * nso * nso;
    jk_memory -=  4 * nA * nso;
    jk_memory -=  4 * nB * nso;
    if (jk_memory < 0L) {
        throw PSIEXCEPTION("Too little static memory for DFTSAPT::fock_terms");
    }
    jk->set_memory((unsigned long int )jk_memory);

    // ==> Generalized Fock Source Terms [Elst/Exch] <== //

    // => Steric Interaction Density Terms (T) <= //

    boost::shared_ptr<Matrix> Sij = build_Sij(S);
    boost::shared_ptr<Matrix> Sij_n = build_Sij_n(Sij);

    std::map<std::string, boost::shared_ptr<Matrix> > Cbar_n = build_Cbar(Sij_n);
    boost::shared_ptr<Matrix> C_T_A_n  = Cbar_n["C_T_A"];
    boost::shared_ptr<Matrix> C_T_B_n  = Cbar_n["C_T_B"];
    boost::shared_ptr<Matrix> C_T_AB_n = Cbar_n["C_T_AB"];
    boost::shared_ptr<Matrix> C_T_BA_n = Cbar_n["C_T_BA"];

    Sij.reset();
    Sij_n.reset();

    boost::shared_ptr<Matrix> C_AS = Matrix::triplet(P_B,S,Cocc_A_);

    // => Load the JK Object <= //

    std::vector<SharedMatrix>& Cl = jk->C_left();
    std::vector<SharedMatrix>& Cr = jk->C_right();
    Cl.clear();
    Cr.clear();

    // J/K[P^A]
    Cl.push_back(Cocc_A_);
    Cr.push_back(Cocc_A_);
    // J/K[T^A, S^\infty]
    Cl.push_back(Cocc_A_);
    Cr.push_back(C_T_A_n);
    // J/K[T^AB, S^\infty]
    Cl.push_back(Cocc_A_);
    Cr.push_back(C_T_AB_n);
    // J/K[S_as]
    Cl.push_back(Cocc_A_);
    Cr.push_back(C_AS);
    // J/K[P^B]
    Cl.push_back(Cocc_B_);
    Cr.push_back(Cocc_B_);

    // => Initialize the JK object <= //

    jk->set_do_J(true);
    jk->set_do_K(true);
    jk->initialize();
    jk->print_header();

    // => Compute the JK matrices <= //

    jk->compute();

    // => Unload the JK Object <= //

    const std::vector<SharedMatrix>& J = jk->J();
    const std::vector<SharedMatrix>& K = jk->K();

    boost::shared_ptr<Matrix> J_A      = J[0];
    boost::shared_ptr<Matrix> J_T_A_n  = J[1];
    boost::shared_ptr<Matrix> J_T_AB_n = J[2];
    boost::shared_ptr<Matrix> J_AS     = J[3];
    boost::shared_ptr<Matrix> J_B      = J[4];

    boost::shared_ptr<Matrix> K_A      = K[0];
    boost::shared_ptr<Matrix> K_T_A_n  = K[1];
    boost::shared_ptr<Matrix> K_T_AB_n = K[2];
    boost::shared_ptr<Matrix> K_AS     = K[3];
    boost::shared_ptr<Matrix> K_B      = K[4];

    // ==> Electrostatic Terms <== //

    // Classical physics (watch for cancellation!)
    double Enuc = 0.0;
    Enuc += dimer_->nuclear_repulsion_energy();
    Enuc -= monomer_A_->nuclear_repulsion_energy();
    Enuc -= monomer_B_->nuclear_repulsion_energy();

    double Elst10 = 0.0;
    std::vector<double> Elst10_terms;
    Elst10_terms.resize(4);
    Elst10_terms[0] += 2.0 * D_A->vector_dot(V_B);
    Elst10_terms[1] += 2.0 * D_B->vector_dot(V_A);
    Elst10_terms[2] += 4.0 * D_B->vector_dot(J_A);
    Elst10_terms[3] += 1.0 * Enuc;
    for (int k = 0; k < Elst10_terms.size(); k++) {
        Elst10 += Elst10_terms[k];
    }
    if (debug_) {
        for (int k = 0; k < Elst10_terms.size(); k++) {
            fprintf(outfile,"    Elst10,r (%1d)        = %18.12lf H\n",k+1,Elst10_terms[k]);
        }
    }
    energies_["Elst10,r"] = Elst10;
    fprintf(outfile,"    Elst10,r            = %18.12lf H\n",Elst10);
    fflush(outfile);

    // ==> Exchange Terms (S^\infty) <== //

    // => Compute the T matrices <= //

    boost::shared_ptr<Matrix> T_A_n  = Matrix::doublet(Cocc_A_, C_T_A_n, false, true);
    boost::shared_ptr<Matrix> T_B_n  = Matrix::doublet(Cocc_B_, C_T_B_n, false, true);
    boost::shared_ptr<Matrix> T_BA_n = Matrix::doublet(Cocc_B_, C_T_BA_n, false, true);
    boost::shared_ptr<Matrix> T_AB_n = Matrix::doublet(Cocc_A_, C_T_AB_n, false, true);

    C_T_A_n.reset();
    C_T_B_n.reset();
    C_T_BA_n.reset();
    C_T_AB_n.reset();

    double Exch10_n = 0.0;
    std::vector<double> Exch10_n_terms;
    Exch10_n_terms.resize(9);
    Exch10_n_terms[0] -= 2.0 * D_A->vector_dot(K_B);
    Exch10_n_terms[1] += 2.0 * T_A_n->vector_dot(V_B);
    Exch10_n_terms[1] += 4.0 * T_A_n->vector_dot(J_B);
    Exch10_n_terms[1] -= 2.0 * T_A_n->vector_dot(K_B);
    Exch10_n_terms[2] += 2.0 * T_B_n->vector_dot(V_A);
    Exch10_n_terms[2] += 4.0 * T_B_n->vector_dot(J_A);
    Exch10_n_terms[2] -= 2.0 * T_B_n->vector_dot(K_A);
    Exch10_n_terms[3] += 2.0 * T_AB_n->vector_dot(V_A);
    Exch10_n_terms[3] += 4.0 * T_AB_n->vector_dot(J_A);
    Exch10_n_terms[3] -= 2.0 * T_AB_n->vector_dot(K_A);
    Exch10_n_terms[4] += 2.0 * T_AB_n->vector_dot(V_B);
    Exch10_n_terms[4] += 4.0 * T_AB_n->vector_dot(J_B);
    Exch10_n_terms[4] -= 2.0 * T_AB_n->vector_dot(K_B);
    Exch10_n_terms[5] += 4.0 * T_B_n->vector_dot(J_T_AB_n);
    Exch10_n_terms[5] -= 2.0 * T_B_n->vector_dot(K_T_AB_n);
    Exch10_n_terms[6] += 4.0 * T_A_n->vector_dot(J_T_AB_n);
    Exch10_n_terms[6] -= 2.0 * T_A_n->vector_dot(K_T_AB_n);
    Exch10_n_terms[7] += 4.0 * T_B_n->vector_dot(J_T_A_n);
    Exch10_n_terms[7] -= 2.0 * T_B_n->vector_dot(K_T_A_n);
    Exch10_n_terms[8] += 4.0 * T_AB_n->vector_dot(J_T_AB_n);
    Exch10_n_terms[8] -= 2.0 * T_AB_n->vector_dot(K_T_AB_n);
    for (int k = 0; k < Exch10_n_terms.size(); k++) {
        Exch10_n += Exch10_n_terms[k];
    }
    if (debug_) {
        for (int k = 0; k < Exch10_n_terms.size(); k++) {
            fprintf(outfile,"    Exch10 (%1d)          = %18.12lf H\n",k+1,Exch10_n_terms[k]);
        }
    }
    energies_["Exch10"] = Exch10_n;
    fprintf(outfile,"    Exch10              = %18.12lf H\n",Exch10_n);
    fflush(outfile);

    T_A_n.reset();
    T_B_n.reset();
    T_BA_n.reset();
    T_AB_n.reset();
    J_T_A_n.reset();
    J_T_AB_n.reset();
    K_T_A_n.reset();
    K_T_AB_n.reset();

    // ==> Exchange Terms (S^2) <== //

    double Exch10_2 = 0.0;
    std::vector<double> Exch10_2_terms;
    Exch10_2_terms.resize(3);
    Exch10_2_terms[0] -= 2.0 * Matrix::triplet(Matrix::triplet(D_A,S,D_B),S,P_A)->vector_dot(V_B);
    Exch10_2_terms[0] -= 4.0 * Matrix::triplet(Matrix::triplet(D_A,S,D_B),S,P_A)->vector_dot(J_B);
    Exch10_2_terms[1] -= 2.0 * Matrix::triplet(Matrix::triplet(D_B,S,D_A),S,P_B)->vector_dot(V_A);
    Exch10_2_terms[1] -= 4.0 * Matrix::triplet(Matrix::triplet(D_B,S,D_A),S,P_B)->vector_dot(J_A);
    Exch10_2_terms[2] -= 2.0 * Matrix::triplet(P_A,S,D_B)->vector_dot(K_AS);
    for (int k = 0; k < Exch10_2_terms.size(); k++) {
        Exch10_2 += Exch10_2_terms[k];
    }
    if (debug_) {
        for (int k = 0; k < Exch10_2_terms.size(); k++) {
            fprintf(outfile,"    Exch10(S^2) (%1d)     = %18.12lf H\n",k+1,Exch10_2_terms[k]);
        }
    }
    energies_["Exch10(S^2)"] = Exch10_2;
    fprintf(outfile,"    Exch10(S^2)         = %18.12lf H\n",Exch10_2);
    fprintf(outfile, "\n");
    fflush(outfile);

    // Clear up some memory
    J_AS.reset();
    K_AS.reset();

    // ==> Uncorrelated Second-Order Response Terms [Induction] <== //

    // => ExchInd pertubations <= //

    boost::shared_ptr<Matrix> C_O_A = Matrix::triplet(D_B,S,Cocc_A_);
    boost::shared_ptr<Matrix> C_P_A = Matrix::triplet(Matrix::triplet(D_B,S,D_A),S,Cocc_B_);
    boost::shared_ptr<Matrix> C_P_B = Matrix::triplet(Matrix::triplet(D_A,S,D_B),S,Cocc_A_);

    Cl.clear();
    Cr.clear();

    // J/K[O]
    Cl.push_back(Cocc_A_);
    Cr.push_back(C_O_A);
    // J/K[P_B]
    Cl.push_back(Cocc_A_);
    Cr.push_back(C_P_B);    
    // J/K[P_B]
    Cl.push_back(Cocc_B_);
    Cr.push_back(C_P_A);

    // => Compute the JK matrices <= //

    jk->compute();

    // => Unload the JK Object <= //

    boost::shared_ptr<Matrix> J_O      = J[0];
    boost::shared_ptr<Matrix> J_P_B    = J[1];
    boost::shared_ptr<Matrix> J_P_A    = J[2];

    boost::shared_ptr<Matrix> K_O      = K[0];
    boost::shared_ptr<Matrix> K_P_B    = K[1];
    boost::shared_ptr<Matrix> K_P_A    = K[2];

    // ==> Generalized ESP (Flat and Exchange) <== //

    std::map<std::string, boost::shared_ptr<Matrix> > mapA;
    mapA["Cocc_A"] = Cocc_A_;
    mapA["Cvir_A"] = Cvir_A_;
    mapA["S"] = S;
    mapA["D_A"] = D_A;
    mapA["V_A"] = V_A;
    mapA["J_A"] = J_A;
    mapA["K_A"] = K_A;
    mapA["D_B"] = D_B;
    mapA["V_B"] = V_B;
    mapA["J_B"] = J_B;
    mapA["K_B"] = K_B;
    mapA["J_O"] = J_O;
    mapA["K_O"] = K_O;
    mapA["J_P"] = J_P_A; 

    boost::shared_ptr<Matrix> wB = build_ind_pot(mapA);
    boost::shared_ptr<Matrix> uB = build_exch_ind_pot(mapA);

    K_O->transpose_this();

    std::map<std::string, boost::shared_ptr<Matrix> > mapB;
    mapB["Cocc_A"] = Cocc_B_;
    mapB["Cvir_A"] = Cvir_B_;
    mapB["S"] = S;
    mapB["D_A"] = D_B;
    mapB["V_A"] = V_B;
    mapB["J_A"] = J_B;
    mapB["K_A"] = K_B;
    mapB["D_B"] = D_A;
    mapB["V_B"] = V_A;
    mapB["J_B"] = J_A;
    mapB["K_B"] = K_A;
    mapB["J_O"] = J_O;
    mapB["K_O"] = K_O;
    mapB["J_P"] = J_P_B; 

    boost::shared_ptr<Matrix> wA = build_ind_pot(mapB);
    boost::shared_ptr<Matrix> uA = build_exch_ind_pot(mapB);

    K_O->transpose_this();

    // ==> Uncoupled Induction <== //

    boost::shared_ptr<Matrix> xuA(wB->clone());
    boost::shared_ptr<Matrix> xuB(wA->clone());

    {
        int na = eps_occ_A_->dimpi()[0];
        int nb = eps_occ_B_->dimpi()[0];
        int nr = eps_vir_A_->dimpi()[0];
        int ns = eps_vir_B_->dimpi()[0];

        double** xuAp = xuA->pointer();
        double** xuBp = xuB->pointer();
        double** wAp = wA->pointer();
        double** wBp = wB->pointer();
        double*  eap = eps_occ_A_->pointer();
        double*  erp = eps_vir_A_->pointer();
        double*  ebp = eps_occ_B_->pointer();
        double*  esp = eps_vir_B_->pointer();

        for (int a = 0; a < na; a++) {
            for (int r = 0; r < nr; r++) {
                xuAp[a][r] = wBp[a][r] / (eap[a] - erp[r]);
            }
        }

        for (int b = 0; b < nb; b++) {
            for (int s = 0; s < ns; s++) {
                xuBp[b][s] = wAp[b][s] / (ebp[b] - esp[s]);
            }
        }
    }

    // ==> Induction <== //

    double Ind20u_AB = 2.0 * xuA->vector_dot(wB);
    double Ind20u_BA = 2.0 * xuB->vector_dot(wA);
    double Ind20u = Ind20u_AB + Ind20u_BA;
    energies_["Ind20,u (A<-B)"] = Ind20u_AB;
    energies_["Ind20,u (B->A)"] = Ind20u_BA;
    energies_["Ind20,u"] = Ind20u;
    fprintf(outfile,"    Ind20,u (A<-B)      = %18.12lf H\n",Ind20u_AB);
    fprintf(outfile,"    Ind20,u (A->B)      = %18.12lf H\n",Ind20u_BA);
    fprintf(outfile,"    Ind20,u             = %18.12lf H\n",Ind20u);
    fflush(outfile);

    // => Exchange-Induction <= //

    double ExchInd20u_AB = 2.0 * xuA->vector_dot(uB);
    double ExchInd20u_BA = 2.0 * xuB->vector_dot(uA);
    double ExchInd20u = ExchInd20u_AB + ExchInd20u_BA;
    fprintf(outfile,"    Exch-Ind20,u (A<-B) = %18.12lf H\n",ExchInd20u_AB);
    fprintf(outfile,"    Exch-Ind20,u (B<-A) = %18.12lf H\n",ExchInd20u_BA);
    fprintf(outfile,"    Exch-Ind20,u        = %18.12lf H\n",ExchInd20u);
    fprintf(outfile,"\n");
    fflush(outfile);

    energies_["Exch-Ind20,u (A<-B)"] = ExchInd20u_AB;
    energies_["Exch-Ind20,u (B->A)"] = ExchInd20u_BA;
    energies_["Exch-Ind20,u"] = ExchInd20u_AB + ExchInd20u_BA;

    // => Coupled Induction <= //

    // Compute CPKS
    std::pair<boost::shared_ptr<Matrix>, boost::shared_ptr<Matrix> > x_sol = compute_x(jk,wB,wA);
    boost::shared_ptr<Matrix> xA = x_sol.first;
    boost::shared_ptr<Matrix> xB = x_sol.second;

    // Backward in Ed's convention
    xA->scale(-1.0);
    xB->scale(-1.0);

    // => Induction <= //

    double Ind20r_AB = 2.0 * xA->vector_dot(wB);
    double Ind20r_BA = 2.0 * xB->vector_dot(wA);
    double Ind20r = Ind20r_AB + Ind20r_BA;
    energies_["Ind20,r (A<-B)"] = Ind20r_AB;
    energies_["Ind20,r (B->A)"] = Ind20r_BA;
    energies_["Ind20,r"] = Ind20r;
    fprintf(outfile,"    Ind20,r (A<-B)      = %18.12lf H\n",Ind20r_AB);
    fprintf(outfile,"    Ind20,r (A->B)      = %18.12lf H\n",Ind20r_BA);
    fprintf(outfile,"    Ind20,r             = %18.12lf H\n",Ind20r);
    fflush(outfile);

    // => Exchange-Induction <= //

    double ExchInd20r_AB = 2.0 * xA->vector_dot(uB);
    double ExchInd20r_BA = 2.0 * xB->vector_dot(uA);
    double ExchInd20r = ExchInd20r_AB + ExchInd20r_BA;
    fprintf(outfile,"    Exch-Ind20,r (A<-B) = %18.12lf H\n",ExchInd20r_AB);
    fprintf(outfile,"    Exch-Ind20,r (B<-A) = %18.12lf H\n",ExchInd20r_BA);
    fprintf(outfile,"    Exch-Ind20,r        = %18.12lf H\n",ExchInd20r);
    fprintf(outfile,"\n");
    fflush(outfile);

    energies_["Exch-Ind20,r (A<-B)"] = ExchInd20r_AB;
    energies_["Exch-Ind20,r (B->A)"] = ExchInd20r_BA;
    energies_["Exch-Ind20,r"] = ExchInd20r_AB + ExchInd20r_BA;

    vars_["S"]   = S;
    vars_["D_A"] = D_A;
    vars_["P_A"] = P_A;
    vars_["V_A"] = V_A;
    vars_["J_A"] = J_A;
    vars_["K_A"] = K_A;
    vars_["D_B"] = D_B;
    vars_["P_B"] = P_B;
    vars_["V_B"] = V_B;
    vars_["J_B"] = J_B;
    vars_["K_B"] = K_B;
    vars_["J_O"] = J_O;
    vars_["K_O"] = K_O;
    vars_["J_P_A"] = J_P_A;
    vars_["J_P_B"] = J_P_B;
}
void DFTSAPT::mp2_terms()
{
    fprintf(outfile, "  PT2 TERMS:\n\n");

    // => Sizing <= //

    int nn = primary_->nbf();

    int na = Caocc_A_->colspi()[0];
    int nb = Caocc_B_->colspi()[0];
    int nr = Cavir_A_->colspi()[0];
    int ns = Cavir_B_->colspi()[0];
    int nQ = mp2fit_->nbf();
    size_t nrQ = nr * (size_t) nQ;
    size_t nsQ = ns * (size_t) nQ;

    int nT = 1;
    #ifdef _OPENMP
        nT = omp_get_max_threads();
    #endif

    // => Stashed Variables <= //

    boost::shared_ptr<Matrix> S   = vars_["S"];
    boost::shared_ptr<Matrix> D_A = vars_["D_A"];
    boost::shared_ptr<Matrix> P_A = vars_["P_A"];
    boost::shared_ptr<Matrix> V_A = vars_["V_A"];
    boost::shared_ptr<Matrix> J_A = vars_["J_A"];
    boost::shared_ptr<Matrix> K_A = vars_["K_A"];
    boost::shared_ptr<Matrix> D_B = vars_["D_B"];
    boost::shared_ptr<Matrix> P_B = vars_["P_B"];
    boost::shared_ptr<Matrix> V_B = vars_["V_B"];
    boost::shared_ptr<Matrix> J_B = vars_["J_B"];
    boost::shared_ptr<Matrix> K_B = vars_["K_B"];
    boost::shared_ptr<Matrix> K_O = vars_["K_O"];

    // => Auxiliary C matrices <= //

    boost::shared_ptr<Matrix> Cr1 = Matrix::triplet(D_B,S,Cavir_A_);
    Cr1->scale(-1.0);
    Cr1->add(Cavir_A_);
    boost::shared_ptr<Matrix> Cs1 = Matrix::triplet(D_A,S,Cavir_B_);
    Cs1->scale(-1.0);
    Cs1->add(Cavir_B_);
    boost::shared_ptr<Matrix> Ca2 = Matrix::triplet(D_B,S,Caocc_A_);
    boost::shared_ptr<Matrix> Cb2 = Matrix::triplet(D_A,S,Caocc_B_);
    boost::shared_ptr<Matrix> Cr3 = Matrix::triplet(D_B,S,Cavir_A_);
    boost::shared_ptr<Matrix> CrX = Matrix::triplet(Matrix::triplet(D_A,S,D_B),S,Cavir_A_);
    Cr3->subtract(CrX);
    Cr3->scale(2.0);
    boost::shared_ptr<Matrix> Cs3 = Matrix::triplet(D_A,S,Cavir_B_);
    boost::shared_ptr<Matrix> CsX = Matrix::triplet(Matrix::triplet(D_B,S,D_A),S,Cavir_B_);
    Cs3->subtract(CsX);
    Cs3->scale(2.0);
    boost::shared_ptr<Matrix> Ca4 = Matrix::triplet(Matrix::triplet(D_A,S,D_B),S,Caocc_A_);
    Ca4->scale(-2.0);
    boost::shared_ptr<Matrix> Cb4 = Matrix::triplet(Matrix::triplet(D_B,S,D_A),S,Caocc_B_);
    Cb4->scale(-2.0);

    // => Auxiliary V matrices <= //

    boost::shared_ptr<Matrix> Jbr = Matrix::triplet(Caocc_B_,J_A,Cavir_A_,true,false,false);
    Jbr->scale(2.0);
    boost::shared_ptr<Matrix> Kbr = Matrix::triplet(Caocc_B_,K_A,Cavir_A_,true,false,false);
    Kbr->scale(-1.0);

    boost::shared_ptr<Matrix> Jas = Matrix::triplet(Caocc_A_,J_B,Cavir_B_,true,false,false);
    Jas->scale(2.0);
    boost::shared_ptr<Matrix> Kas = Matrix::triplet(Caocc_A_,K_B,Cavir_B_,true,false,false);
    Kas->scale(-1.0);

    boost::shared_ptr<Matrix> KOas = Matrix::triplet(Caocc_A_,K_O,Cavir_B_,true,false,false);
    KOas->scale(1.0);
    boost::shared_ptr<Matrix> KObr = Matrix::triplet(Caocc_B_,K_O,Cavir_A_,true,true,false);
    KObr->scale(1.0);

    boost::shared_ptr<Matrix> JBas = Matrix::triplet(Matrix::triplet(Caocc_A_,S,D_B,true,false,false),J_A,Cavir_B_);
    JBas->scale(-2.0);
    boost::shared_ptr<Matrix> JAbr = Matrix::triplet(Matrix::triplet(Caocc_B_,S,D_A,true,false,false),J_B,Cavir_A_);
    JAbr->scale(-2.0);

    boost::shared_ptr<Matrix> Jbs = Matrix::triplet(Caocc_B_,J_A,Cavir_B_,true,false,false);
    Jbs->scale(4.0);
    boost::shared_ptr<Matrix> Jar = Matrix::triplet(Caocc_A_,J_B,Cavir_A_,true,false,false);
    Jar->scale(4.0);

    boost::shared_ptr<Matrix> JAas = Matrix::triplet(Matrix::triplet(Caocc_A_,J_B,D_A,true,false,false),S,Cavir_B_);
    JAas->scale(-2.0);
    boost::shared_ptr<Matrix> JBbr = Matrix::triplet(Matrix::triplet(Caocc_B_,J_A,D_B,true,false,false),S,Cavir_A_);
    JBbr->scale(-2.0);

    // Get your signs right Hesselmann!
    boost::shared_ptr<Matrix> Vbs = Matrix::triplet(Caocc_B_,V_A,Cavir_B_,true,false,false);
    Vbs->scale(2.0);
    boost::shared_ptr<Matrix> Var = Matrix::triplet(Caocc_A_,V_B,Cavir_A_,true,false,false);
    Var->scale(2.0);
    boost::shared_ptr<Matrix> VBas = Matrix::triplet(Matrix::triplet(Caocc_A_,S,D_B,true,false,false),V_A,Cavir_B_);
    VBas->scale(-1.0);
    boost::shared_ptr<Matrix> VAbr = Matrix::triplet(Matrix::triplet(Caocc_B_,S,D_A,true,false,false),V_B,Cavir_A_);
    VAbr->scale(-1.0);
    boost::shared_ptr<Matrix> VRas = Matrix::triplet(Matrix::triplet(Caocc_A_,V_B,P_A,true,false,false),S,Cavir_B_);
    VRas->scale(1.0);
    boost::shared_ptr<Matrix> VSbr = Matrix::triplet(Matrix::triplet(Caocc_B_,V_A,P_B,true,false,false),S,Cavir_A_);
    VSbr->scale(1.0);

    boost::shared_ptr<Matrix> Sas = Matrix::triplet(Caocc_A_,S,Cavir_B_,true,false,false);
    boost::shared_ptr<Matrix> Sbr = Matrix::triplet(Caocc_B_,S,Cavir_A_,true,false,false);

    boost::shared_ptr<Matrix> Qbr(Jbr->clone());
    Qbr->zero();
    Qbr->add(Jbr);
    Qbr->add(Kbr);
    Qbr->add(KObr);
    Qbr->add(JAbr);
    Qbr->add(JBbr);
    Qbr->add(VAbr);
    Qbr->add(VSbr);

    boost::shared_ptr<Matrix> Qas(Jas->clone());
    Qas->zero();
    Qas->add(Jas);
    Qas->add(Kas);
    Qas->add(KOas);
    Qas->add(JAas);
    Qas->add(JBas);
    Qas->add(VBas);
    Qas->add(VRas);

    boost::shared_ptr<Matrix> SBar = Matrix::triplet(Matrix::triplet(Caocc_A_,S,D_B,true,false,false),S,Cavir_A_);
    boost::shared_ptr<Matrix> SAbs = Matrix::triplet(Matrix::triplet(Caocc_B_,S,D_A,true,false,false),S,Cavir_B_);

    boost::shared_ptr<Matrix> Qar(Jar->clone());
    Qar->zero();
    Qar->add(Jar);
    Qar->add(Var);

    boost::shared_ptr<Matrix> Qbs(Jbs->clone());
    Qbs->zero();
    Qbs->add(Jbs);
    Qbs->add(Vbs);

    Jbr.reset();
    Kbr.reset();
    Jas.reset();
    Kas.reset();
    KOas.reset();
    KObr.reset();
    JBas.reset();
    JAbr.reset();
    Jbs.reset();
    Jar.reset();
    JAas.reset();
    JBbr.reset();
    Vbs.reset();
    Var.reset();
    VBas.reset();
    VAbr.reset();
    VRas.reset();
    VSbr.reset();

    S.reset();
    D_A.reset();
    P_A.reset();
    V_A.reset();
    J_A.reset();
    K_A.reset();
    D_B.reset();
    P_B.reset();
    V_B.reset();
    J_B.reset();
    K_B.reset();
    K_O.reset();

    vars_.clear();

    // => Memory <= //

    // => Integrals from the THCE <= //

    boost::shared_ptr<DFERI> df = DFERI::build(primary_,mp2fit_,Process::environment.options);
    df->clear();

    std::vector<boost::shared_ptr<Matrix> > Cs;
    Cs.push_back(Caocc_A_);
    Cs.push_back(Cavir_A_);
    Cs.push_back(Caocc_B_);
    Cs.push_back(Cavir_B_);
    Cs.push_back(Cr1);
    Cs.push_back(Cs1);
    Cs.push_back(Ca2);
    Cs.push_back(Cb2);
    Cs.push_back(Cr3);
    Cs.push_back(Cs3);
    Cs.push_back(Ca4);
    Cs.push_back(Cb4);
    boost::shared_ptr<Matrix> Call = Matrix::horzcat(Cs);
    Cs.clear();

    df->set_C(Call);
    df->set_memory(memory_ - Call->nrow() * Call->ncol());

    int offset = 0;
    df->add_space("a",offset,offset+Caocc_A_->colspi()[0]); offset += Caocc_A_->colspi()[0];
    df->add_space("r",offset,offset+Cavir_A_->colspi()[0]); offset += Cavir_A_->colspi()[0];
    df->add_space("b",offset,offset+Caocc_B_->colspi()[0]); offset += Caocc_B_->colspi()[0];
    df->add_space("s",offset,offset+Cavir_B_->colspi()[0]); offset += Cavir_B_->colspi()[0];
    df->add_space("r1",offset,offset+Cr1->colspi()[0]); offset += Cr1->colspi()[0];
    df->add_space("s1",offset,offset+Cs1->colspi()[0]); offset += Cs1->colspi()[0];
    df->add_space("a2",offset,offset+Ca2->colspi()[0]); offset += Ca2->colspi()[0];
    df->add_space("b2",offset,offset+Cb2->colspi()[0]); offset += Cb2->colspi()[0];
    df->add_space("r3",offset,offset+Cr3->colspi()[0]); offset += Cr3->colspi()[0];
    df->add_space("s3",offset,offset+Cs3->colspi()[0]); offset += Cs3->colspi()[0];
    df->add_space("a4",offset,offset+Ca4->colspi()[0]); offset += Ca4->colspi()[0];
    df->add_space("b4",offset,offset+Cb4->colspi()[0]); offset += Cb4->colspi()[0];

    df->add_pair_space("Aar", "a", "r");
    df->add_pair_space("Abs", "b", "s");
    df->add_pair_space("Bas", "a", "s1");
    df->add_pair_space("Bbr", "b", "r1");
    df->add_pair_space("Cas", "a2","s");
    df->add_pair_space("Cbr", "b2","r");
    df->add_pair_space("Dar", "a", "r3");
    df->add_pair_space("Dbs", "b", "s3");
    df->add_pair_space("Ear", "a4","r");
    df->add_pair_space("Ebs", "b4","s");

    Cr1.reset();
    Cs1.reset();
    Ca2.reset();
    Cb2.reset();
    Cr3.reset();
    Cs3.reset();
    Ca4.reset();
    Cb4.reset();
    Call.reset();

    df->print_header();
    df->compute();

    std::map<std::string, boost::shared_ptr<Tensor> >& ints = df->ints();

    boost::shared_ptr<Tensor> AarT = ints["Aar"];
    boost::shared_ptr<Tensor> AbsT = ints["Abs"];
    boost::shared_ptr<Tensor> BasT = ints["Bas"];
    boost::shared_ptr<Tensor> BbrT = ints["Bbr"];
    boost::shared_ptr<Tensor> CasT = ints["Cas"];
    boost::shared_ptr<Tensor> CbrT = ints["Cbr"];
    boost::shared_ptr<Tensor> DarT = ints["Dar"];
    boost::shared_ptr<Tensor> DbsT = ints["Dbs"];
    boost::shared_ptr<Tensor> EarT = ints["Ear"];
    boost::shared_ptr<Tensor> EbsT = ints["Ebs"];

    df.reset();

    // => Blocking <= //

    long int overhead = 0L;
    overhead += 2L * nT * nr * ns;
    overhead += 2L * na * ns + 2L * nb * nr + 2L * na * nr + 2L * nb * ns;
    long int rem = memory_ - overhead;

    if (rem < 0L) {
        throw PSIEXCEPTION("Too little static memory for DFTSAPT::mp2_terms");
    }

    long int cost_a = 2L * nr * nQ + 2L * ns * nQ;
    long int max_a = rem / (2L * cost_a);
    long int max_b = max_a;
    max_a = (max_a > na ? na : max_a);
    max_b = (max_b > nb ? nb : max_b);
    if (max_a < 1L || max_b < 1L) {
        throw PSIEXCEPTION("Too little dynamic memory for DFTSAPT::mp2_terms");
    }

    // => Tensor Slices <= //

    boost::shared_ptr<Matrix> Aar(new Matrix("Aar",max_a*nr,nQ));
    boost::shared_ptr<Matrix> Abs(new Matrix("Abs",max_b*ns,nQ));
    boost::shared_ptr<Matrix> Bas(new Matrix("Bas",max_a*ns,nQ));
    boost::shared_ptr<Matrix> Bbr(new Matrix("Bbr",max_b*nr,nQ));
    boost::shared_ptr<Matrix> Cas(new Matrix("Cas",max_a*ns,nQ));
    boost::shared_ptr<Matrix> Cbr(new Matrix("Cbr",max_b*nr,nQ));
    boost::shared_ptr<Matrix> Dar(new Matrix("Dar",max_a*nr,nQ));
    boost::shared_ptr<Matrix> Dbs(new Matrix("Dbs",max_b*ns,nQ));

    // => Thread Work Arrays <= //

    std::vector<boost::shared_ptr<Matrix> > Trs;
    std::vector<boost::shared_ptr<Matrix> > Vrs;
    for (int t = 0; t < nT; t++) {
        Trs.push_back(boost::shared_ptr<Matrix>(new Matrix("Trs",nr,ns)));
        Vrs.push_back(boost::shared_ptr<Matrix>(new Matrix("Vrs",nr,ns)));
    }

    // => Pointers <= //

    double** Aarp = Aar->pointer();
    double** Absp = Abs->pointer();
    double** Basp = Bas->pointer();
    double** Bbrp = Bbr->pointer();
    double** Casp = Cas->pointer();
    double** Cbrp = Cbr->pointer();
    double** Darp = Dar->pointer();
    double** Dbsp = Dbs->pointer();

    double** Sasp = Sas->pointer();
    double** Sbrp = Sbr->pointer();
    double** SBarp = SBar->pointer();
    double** SAbsp = SAbs->pointer();

    double** Qasp = Qas->pointer();
    double** Qbrp = Qbr->pointer();
    double** Qarp = Qar->pointer();
    double** Qbsp = Qbs->pointer();

    double*  eap  = eps_aocc_A_->pointer();
    double*  ebp  = eps_aocc_B_->pointer();
    double*  erp  = eps_avir_A_->pointer();
    double*  esp  = eps_avir_B_->pointer();

    // => File Pointers <= //

    FILE* Aarf = AarT->file_pointer();
    FILE* Absf = AbsT->file_pointer();
    FILE* Basf = BasT->file_pointer();
    FILE* Bbrf = BbrT->file_pointer();
    FILE* Casf = CasT->file_pointer();
    FILE* Cbrf = CbrT->file_pointer();
    FILE* Darf = DarT->file_pointer();
    FILE* Dbsf = DbsT->file_pointer();
    FILE* Earf = EarT->file_pointer();
    FILE* Ebsf = EbsT->file_pointer();

    // => Slice D + E -> D <= //

    fseek(Darf,0L,SEEK_SET);
    fseek(Earf,0L,SEEK_SET);
    for (int astart = 0; astart < na; astart += max_a) {
        int nablock = (astart + max_a >= na ? na - astart : max_a);
        fread(Darp[0],sizeof(double),nablock*nrQ,Darf);
        fread(Aarp[0],sizeof(double),nablock*nrQ,Earf);
        C_DAXPY(nablock*nrQ,1.0,Aarp[0],1,Darp[0],1);
        fseek(Darf,sizeof(double)*astart*nrQ,SEEK_SET);
        fwrite(Darp[0],sizeof(double),nablock*nrQ,Darf);
    }

    fseek(Dbsf,0L,SEEK_SET);
    fseek(Ebsf,0L,SEEK_SET);
    for (int bstart = 0; bstart < nb; bstart += max_b) {
        int nbblock = (bstart + max_b >= nb ? nb - bstart : max_b);
        fread(Dbsp[0],sizeof(double),nbblock*nsQ,Dbsf);
        fread(Absp[0],sizeof(double),nbblock*nsQ,Ebsf);
        C_DAXPY(nbblock*nsQ,1.0,Absp[0],1,Dbsp[0],1);
        fseek(Dbsf,sizeof(double)*bstart*nsQ,SEEK_SET);
        fwrite(Dbsp[0],sizeof(double),nbblock*nsQ,Dbsf);
    }

    // => Targets <= //

    double Disp20 = 0.0;
    double ExchDisp20 = 0.0;

    // ==> Master Loop <== //

    fseek(Aarf,0L,SEEK_SET);
    fseek(Basf,0L,SEEK_SET);
    fseek(Casf,0L,SEEK_SET);
    fseek(Darf,0L,SEEK_SET);
    for (int astart = 0; astart < na; astart += max_a) {
        int nablock = (astart + max_a >= na ? na - astart : max_a);

        fread(Aarp[0],sizeof(double),nablock*nrQ,Aarf);
        fread(Basp[0],sizeof(double),nablock*nsQ,Basf);
        fread(Casp[0],sizeof(double),nablock*nsQ,Casf);
        fread(Darp[0],sizeof(double),nablock*nrQ,Darf);

        fseek(Absf,0L,SEEK_SET);
        fseek(Bbrf,0L,SEEK_SET);
        fseek(Cbrf,0L,SEEK_SET);
        fseek(Dbsf,0L,SEEK_SET);
        for (int bstart = 0; bstart < nb; bstart += max_b) {
            int nbblock = (bstart + max_b >= nb ? nb - bstart : max_b);

            fread(Absp[0],sizeof(double),nbblock*nsQ,Absf);
            fread(Bbrp[0],sizeof(double),nbblock*nrQ,Bbrf);
            fread(Cbrp[0],sizeof(double),nbblock*nrQ,Cbrf);
            fread(Dbsp[0],sizeof(double),nbblock*nsQ,Dbsf);

            long int nab = nablock * nbblock;

            #pragma omp parallel for schedule(dynamic) reduction(+: Disp20, ExchDisp20)
            for (long int ab = 0L; ab < nab; ab++) {
                int a = ab / nbblock;
                int b = ab % nbblock;

                int thread = 0;
                #ifdef _OPENMP
                    thread = omp_get_thread_num();
                #endif

                double** Trsp = Trs[thread]->pointer();
                double** Vrsp = Vrs[thread]->pointer();

                // => Amplitudes, Disp20 <= //

                C_DGEMM('N','T',nr,ns,nQ,1.0,Aarp[(a)*nr],nQ,Absp[(b)*ns],nQ,0.0,Vrsp[0],ns);

                for (int r = 0; r < nr; r++) {
                    for (int s = 0; s < ns; s++) {
                        Trsp[r][s] = Vrsp[r][s] / (eap[a + astart] + ebp[b + bstart] - erp[r] - esp[s]);
                        Disp20 += 4.0 * Trsp[r][s] * Vrsp[r][s];
                    }
                }

                // => Exch-Disp20 <= //

                // > Q1-Q3 < //

                C_DGEMM('N','T',nr,ns,nQ,1.0,Bbrp[(b)*nr],nQ,Basp[(a)*ns],nQ,0.0,Vrsp[0],ns);
                C_DGEMM('N','T',nr,ns,nQ,1.0,Cbrp[(b)*nr],nQ,Casp[(a)*ns],nQ,1.0,Vrsp[0],ns);
                C_DGEMM('N','T',nr,ns,nQ,1.0,Aarp[(a)*nr],nQ,Dbsp[(b)*ns],nQ,1.0,Vrsp[0],ns);
                C_DGEMM('N','T',nr,ns,nQ,1.0,Darp[(a)*nr],nQ,Absp[(b)*ns],nQ,1.0,Vrsp[0],ns);

                // > V,J,K < //

                C_DGER(nr,ns,1.0,Qbrp[b + bstart],1,Sasp[a + astart],1,Vrsp[0],ns);
                C_DGER(nr,ns,1.0,Sbrp[b + bstart],1,Qasp[a + astart],1,Vrsp[0],ns);
                C_DGER(nr,ns,1.0,Qarp[a + astart],1,SAbsp[b + bstart],1,Vrsp[0],ns);
                C_DGER(nr,ns,1.0,SBarp[a + astart],1,Qbsp[b + bstart],1,Vrsp[0],ns);

                for (int r = 0; r < nr; r++) {
                    for (int s = 0; s < ns; s++) {
                        ExchDisp20 -= 2.0 * Trsp[r][s] * Vrsp[r][s];
                    }
                }
            }
        }
    }

    energies_["Disp20"] = Disp20;
    energies_["Exch-Disp20"] = ExchDisp20;
    fprintf(outfile,"    Disp20              = %18.12lf H\n",Disp20);
    fprintf(outfile,"    Exch-Disp20         = %18.12lf H\n",ExchDisp20);
    fprintf(outfile,"\n");
    fflush(outfile);
}
void DFTSAPT::tdhf_demo()
{
    fprintf(outfile, "  TDHF Terms:\n\n");
    fflush(outfile);

    // => Sizing <= //

    int na = Caocc_A_->colspi()[0];
    int nb = Caocc_B_->colspi()[0];
    int nr = Cavir_A_->colspi()[0];
    int ns = Cavir_B_->colspi()[0];
    int nQ = mp2fit_->nbf();
    size_t nrQ = nr * (size_t) nQ;
    size_t nsQ = ns * (size_t) nQ;

    int nT = 1;
    #ifdef _OPENMP
        nT = omp_get_max_threads();
    #endif

    // => Integrals from the THCE <= //

    boost::shared_ptr<DFERI> df = DFERI::build(primary_,mp2fit_,Process::environment.options);
    df->clear();

    std::vector<boost::shared_ptr<Matrix> > Cs;
    Cs.push_back(Caocc_A_);
    Cs.push_back(Cavir_A_);
    Cs.push_back(Caocc_B_);
    Cs.push_back(Cavir_B_);
    boost::shared_ptr<Matrix> Call = Matrix::horzcat(Cs);
    Cs.clear();

    df->set_C(Call);
    df->set_memory(memory_ - Call->nrow() * Call->ncol());

    int offset = 0;
    df->add_space("a",offset,offset+Caocc_A_->colspi()[0]); offset += Caocc_A_->colspi()[0];
    df->add_space("r",offset,offset+Cavir_A_->colspi()[0]); offset += Cavir_A_->colspi()[0];
    df->add_space("b",offset,offset+Caocc_B_->colspi()[0]); offset += Caocc_B_->colspi()[0];
    df->add_space("s",offset,offset+Cavir_B_->colspi()[0]); offset += Cavir_B_->colspi()[0];

    df->add_pair_space("Aaa", "a", "a");
    df->add_pair_space("Aar", "a", "r");
    df->add_pair_space("Arr", "r", "r");
    df->add_pair_space("Abb", "b", "b");
    df->add_pair_space("Abs", "b", "s");
    df->add_pair_space("Ass", "s", "s");

    Call.reset();

    df->print_header();
    df->compute();

    std::map<std::string, boost::shared_ptr<Tensor> >& ints = df->ints();

    boost::shared_ptr<Tensor> AaaT = ints["Aaa"];
    boost::shared_ptr<Tensor> AarT = ints["Aar"];
    boost::shared_ptr<Tensor> ArrT = ints["Arr"];
    boost::shared_ptr<Tensor> AbbT = ints["Abb"];
    boost::shared_ptr<Tensor> AbsT = ints["Abs"];
    boost::shared_ptr<Tensor> AssT = ints["Ass"];

    boost::shared_ptr<Matrix> J = df->Jpow(1.0);
    boost::shared_ptr<Matrix> Jm12 = df->Jpow(-1.0/2.0);

    df.reset();

    // ==> Setup <== //

    // =>  D coefficients <= //

    boost::shared_ptr<Tensor> DarT = fitting("DarT",AarT,Jm12);
    boost::shared_ptr<Tensor> DbsT = fitting("DbsT",AbsT,Jm12);
    Jm12.reset();

    // => DF projectors <= //

    boost::shared_ptr<Matrix> D2ar = inner(DarT,DbsT);
    boost::shared_ptr<Matrix> D2bs = inner(DbsT,DbsT);
    D2ar->power(-1.0,1.0E-12);
    D2bs->power(-1.0,1.0E-12);

    // => P projectors <= //

    boost::shared_ptr<Tensor> Par = fitting("ParT",DarT,D2ar);
    boost::shared_ptr<Tensor> Pbs = fitting("PbsT",DbsT,D2bs);
    D2ar.reset();
    D2bs.reset();

    // => F integrals <= //

    // TODO

    // => H(2) D <= //

    // TODO

    //boost::shared_ptr<Tensor> H2DarT = H2_product("H2DarT",eps_aocc_A_,eps_avir_A_,AaaT,AarT,ArrT,DarT);
    //boost::shared_ptr<Tensor> H2DbsT = H2_product("H2DbsT",eps_aocc_B_,eps_avir_B_,AbsT,AbsT,AssT,DbsT);

    // => Variables Stack <= //

    // TODO
    std::map<std::string, boost::shared_ptr<Tensor> > vars;

    // => Frequencies <= //

    boost::shared_ptr<GaussChebyshev> quad(new GaussChebyshev(freq_points_,freq_scale_));
    quad->print_header();
    quad->compute();
    std::vector<double> omega = quad->nodes();
    std::vector<double> alpha = quad->weights();

    // ==> Master Loop <== //

    double Disp20 = 0.0;
    double Disp2C = 0.0;
    std::vector<double> Disp20_terms(omega.size());
    std::vector<double> Disp2C_terms(omega.size());;

    time_t start;
    time_t stop;

    start = time(NULL);
    fprintf(outfile,"    ----------------------------------------------------------------------\n");
    fprintf(outfile,"    %-3s %11s %15s %15s %11s %10s\n", "N", "Omega", "Disp20 [H]", "Disp2C [H]", "Ratio", "Time [s]");
    fprintf(outfile,"    ----------------------------------------------------------------------\n");
    fflush(outfile);

    for (int w = 0; w < omega.size(); w++) {

        boost::shared_ptr<Matrix> UA = uncoupled_susceptibility(omega[w],eps_aocc_A_,eps_avir_A_,DarT);
        boost::shared_ptr<Matrix> UAJ = Matrix::doublet(UA,J);
        UA.reset();
        boost::shared_ptr<Matrix> UB = uncoupled_susceptibility(omega[w],eps_aocc_B_,eps_avir_B_,DbsT);
        boost::shared_ptr<Matrix> UBJ = Matrix::doublet(UB,J);
        UB.reset();
        Disp20_terms[w] -= 1.0 / (2.0 * M_PI) * alpha[w] * UAJ->vector_dot(UBJ->transpose());
        UAJ.reset();
        UBJ.reset();

        boost::shared_ptr<Matrix> CA = coupled_susceptibility_debug(omega[w],eps_aocc_A_,eps_avir_A_,AaaT,AarT,ArrT,DarT);
        boost::shared_ptr<Matrix> CAJ = Matrix::doublet(CA,J);
        CA.reset();
        boost::shared_ptr<Matrix> CB = coupled_susceptibility_debug(omega[w],eps_aocc_B_,eps_avir_B_,AbbT,AbsT,AssT,DbsT);
        boost::shared_ptr<Matrix> CBJ = Matrix::doublet(CB,J);
        CB.reset();
        Disp2C_terms[w] -= 1.0 / (2.0 * M_PI) * alpha[w] * CAJ->vector_dot(CBJ->transpose());
        CAJ.reset();
        CBJ.reset();

        stop = time(NULL);
        fprintf(outfile,"    %-3d %11.3E %15.12lf %15.12lf %11.3E %10ld\n", w+1,omega[w],Disp20_terms[w],Disp2C_terms[w],Disp2C_terms[w] / Disp20_terms[w],stop-start);
        fflush(outfile);

    }

    fprintf(outfile,"    ----------------------------------------------------------------------\n");
    fprintf(outfile,"\n");

    for (int w = 0; w < omega.size(); w++) {
        Disp20 += Disp20_terms[w];
        Disp2C += Disp2C_terms[w];
    }

    fprintf(outfile,"    Disp20              = %18.12lf H\n",Disp20);
    fprintf(outfile,"    Disp2C              = %18.12lf H\n",Disp2C);
    fprintf(outfile,"    Ratio               = %18.12lf\n",Disp2C/Disp20);
    fprintf(outfile,"\n");
    fflush(outfile);
}
boost::shared_ptr<Matrix> DFTSAPT::uncoupled_susceptibility(
    double omega,
    boost::shared_ptr<Vector> ea,
    boost::shared_ptr<Vector> er,
    boost::shared_ptr<Tensor> Bar)
{
    int na = Bar->sizes()[0];
    int nr = Bar->sizes()[1];

    double* eap = ea->pointer();
    double* erp = er->pointer();

    boost::shared_ptr<Matrix> lambda(new Matrix("lambda",na,nr));
    double** lp = lambda->pointer();

    for (int a = 0; a < na; a++) {
        for (int r = 0; r < nr; r++) {
            double ear = erp[r] - eap[a];
            lp[a][r] = 4.0 * ear / (ear * ear + omega * omega);
        }
    }

    boost::shared_ptr<Matrix> U = inner(Bar,Bar,lambda);

    return U;
}
boost::shared_ptr<Matrix> DFTSAPT::coupled_susceptibility(
    double omega,
    boost::shared_ptr<Vector> ea,
    boost::shared_ptr<Vector> er,
    std::map<std::string, boost::shared_ptr<Tensor> >& vars,
    int nmax)
{
    // => Lambda (Preconditioner) <= //

    int na = ea->dimpi()[0];
    int nr = er->dimpi()[0];

    double* eap = ea->pointer();
    double* erp = er->pointer();

    boost::shared_ptr<Matrix> L(new Matrix("L",na,nr));
    double** Lp = L->pointer();

    for (int a = 0; a < na; a++) {
        for (int r = 0; r < nr; r++) {
            double ear = erp[r] - eap[a];
            Lp[a][r] = -4.0 / (ear * ear + omega * omega);
        }
    }

    // => IDF Inverse (Beats the shit out of n^6) <= //

    boost::shared_ptr<Matrix> IDF = inner(vars["DarT"],vars["FparT"],L);
    IDF->scale(-1.0);
    double** IDFp = IDF->pointer();
    int nQ = IDF->colspi()[0];
    for (int Q = 0; Q < nQ; Q++) {
        IDFp[Q][Q] += 1.0;
    }
    // TODO pseudoinverse

    // => k = 0 <= //

    boost::shared_ptr<Matrix> T = inner(vars["DarT"],vars["H2DarT"],L);
    boost::shared_ptr<Matrix> U = Matrix::doublet(IDF,T);
    boost::shared_ptr<Tensor> S = fitting("SarT",vars["FparT"],U);
    axpy(vars["H2DarT"],S,1.0,1.0);
    boost::shared_ptr<Matrix> C = inner(vars["DarT"],S,L);

    // => k > 0 <= //

    for (int k = 1; k < nmax; k++) {
        boost::shared_ptr<Matrix> W = inner(vars["DarT"],S,L);
        boost::shared_ptr<Tensor> V = fitting("VarsT",vars["ParT"],W);
        W.reset();
        axpy(V,S,-1.0,1.0);
        boost::shared_ptr<Tensor> R;
        //boost::shared_ptr<Tensor> R = R_product("RQarT",S,vars); // TODO
        S.reset();
        boost::shared_ptr<Matrix> T = inner(vars["DarT"],R,L);
        boost::shared_ptr<Matrix> U = Matrix::doublet(IDF,T);
        T.reset();
        S = fitting("SarT",vars["FparT"],U);
        U.reset();
        axpy(vars["H2DarT"],S,1.0/4.0,1.0/4.0);
        boost::shared_ptr<Matrix> C2 = inner(vars["DarT"],S,L);
        C->add(C2);
    }

    C->hermitivitize();

    return C;
}
boost::shared_ptr<Matrix> DFTSAPT::coupled_susceptibility_debug(
    double omega,
    boost::shared_ptr<Vector> ea,
    boost::shared_ptr<Vector> er,
    boost::shared_ptr<Tensor> AaaT,
    boost::shared_ptr<Tensor> AarT,
    boost::shared_ptr<Tensor> ArrT,
    boost::shared_ptr<Tensor> DarT)
{
    int na = AarT->sizes()[0];
    int nr = AarT->sizes()[1];
    int nQ = AarT->sizes()[2];

    FILE* Aaaf = AaaT->file_pointer();
    FILE* Aarf = AarT->file_pointer();
    FILE* Arrf = ArrT->file_pointer();
    FILE* Darf = DarT->file_pointer();

    fseek(Aaaf,0L,SEEK_SET);
    fseek(Aarf,0L,SEEK_SET);
    fseek(Arrf,0L,SEEK_SET);
    fseek(Darf,0L,SEEK_SET);

    boost::shared_ptr<Matrix> Aaa(new Matrix("Aaa",na*na,nQ));
    boost::shared_ptr<Matrix> Aar(new Matrix("Aar",na*nr,nQ));
    boost::shared_ptr<Matrix> Arr(new Matrix("Arr",nr*nr,nQ));
    boost::shared_ptr<Matrix> Dar(new Matrix("Dar",na*nr,nQ));
    double** Aaap = Aaa->pointer();
    double** Aarp = Aar->pointer();
    double** Arrp = Arr->pointer();
    double** Darp = Dar->pointer();

    fread(Aaap[0],sizeof(double),na*na*(size_t)nQ,Aaaf);
    fread(Aarp[0],sizeof(double),na*nr*(size_t)nQ,Aarf);
    fread(Arrp[0],sizeof(double),nr*nr*(size_t)nQ,Arrf);
    fread(Darp[0],sizeof(double),na*nr*(size_t)nQ,Darf);

    double* eap = ea->pointer();
    double* erp = er->pointer();

    boost::shared_ptr<Matrix> Iarar(new Matrix("Iarar",na*nr,na*nr));
    boost::shared_ptr<Matrix> Iaarr(new Matrix("Iaarr",na*na,nr*nr));
    double** Iararp = Iarar->pointer();
    double** Iaarrp = Iaarr->pointer();

    C_DGEMM('N','T',na*nr,na*nr,nQ,1.0,Aarp[0],nQ,Aarp[0],nQ,0.0,Iararp[0],na*nr);
    C_DGEMM('N','T',na*na,nr*nr,nQ,1.0,Aaap[0],nQ,Arrp[0],nQ,0.0,Iaarrp[0],nr*nr);

    boost::shared_ptr<Matrix> H1(new Matrix("H1",na*nr,na*nr));
    boost::shared_ptr<Matrix> H2(new Matrix("H2",na*nr,na*nr));
    double** H1p = H1->pointer();
    double** H2p = H2->pointer();

    for (int a1 = 0; a1 < na; a1++) {
    for (int a2 = 0; a2 < na; a2++) {
    for (int r1 = 0; r1 < nr; r1++) {
    for (int r2 = 0; r2 < nr; r2++) {
        H1p[a1*nr+r1][a2*nr+r2] = 4.0 * Iararp[a1*nr+r1][a2*nr+r2] - Iararp[a1*nr+r2][a2*nr+r1] - Iaarrp[a1*na+a2][r1*nr+r2];
        H2p[a1*nr+r1][a2*nr+r2] = Iararp[a1*nr+r2][a2*nr+r1] - Iaarrp[a1*na+a2][r1*nr+r2];
    }}}}

    for (int a1 = 0; a1 < na; a1++) {
    for (int r1 = 0; r1 < nr; r1++) {
        H1p[a1*nr+r1][a1*nr+r1] += (erp[r1] - eap[a1]);
        H2p[a1*nr+r1][a1*nr+r1] += (erp[r1] - eap[a1]);
    }}

    Iarar.reset();
    Iaarr.reset();

    boost::shared_ptr<Matrix> A(new Matrix("A",na*nr,na*nr));
    double** Ap = A->pointer();

    C_DGEMM('N','N',na*nr,na*nr,na*nr,1.0,H2p[0],na*nr,H1p[0],na*nr,0.0,Ap[0],na*nr);

    for (int a1 = 0; a1 < na; a1++) {
    for (int r1 = 0; r1 < nr; r1++) {
        Ap[a1*nr+r1][a1*nr+r1] += omega * omega;
    }}

    A->power(-1.0);

    C_DGEMM('N','N',na*nr,na*nr,na*nr,-4.0,Ap[0],na*nr,H2p[0],na*nr,0.0,H1p[0],na*nr);

    A.reset();
    H2.reset();

    boost::shared_ptr<Matrix> C2(new Matrix("C2",na*nr,nQ));
    boost::shared_ptr<Matrix> C(new Matrix("C",nQ,nQ));
    double** C2p = C2->pointer();
    double** Cp  = C->pointer();

    C_DGEMM('N','N',na*nr,nQ,na*nr,1.0,H1p[0],na*nr,Darp[0],nQ,0.0,C2p[0],nQ);
    C_DGEMM('T','N',nQ,nQ,na*nr,1.0,Darp[0],nQ,C2p[0],nQ,0.0,Cp[0],nQ);

    return C;
}

boost::shared_ptr<Matrix> DFTSAPT::inner(
    boost::shared_ptr<Tensor> LT,
    boost::shared_ptr<Tensor> RT,
    boost::shared_ptr<Matrix> lambda)
{
    // => Sizing <= //

    int na = LT->sizes()[0];
    int nr = LT->sizes()[1];
    int nQ = LT->sizes()[2];

    // => Diagonal (must be SPD) <= //

    double** lp;
    if (lambda) {
        lp = lambda->pointer();
    }

    // => File Pointer <= //

    FILE* Lf = LT->file_pointer();
    FILE* Rf = RT->file_pointer();
    bool symm = (LT == RT);

    // => Blocking <= //

    size_t nrQ = nr * (size_t) nQ;
    long int max_a = ((memory_ - nQ * (size_t) nQ) / (symm ? nrQ : 2L * nrQ));
    max_a = (max_a > na ? na : max_a);
    if (max_a < 1L) {
        throw PSIEXCEPTION("Not enough dynamic memory in DFTSAPT::inner");
    }

    // => Temp <= //

    boost::shared_ptr<Matrix> L(new Matrix("L",max_a*nr,nQ));
    boost::shared_ptr<Matrix> R = (symm ? L : boost::shared_ptr<Matrix>(new Matrix("R",max_a*nr,nQ)));
    double** Lp = L->pointer();
    double** Rp = R->pointer();

    // => Target <= //

    boost::shared_ptr<Matrix> U(new Matrix("U",nQ,nQ));
    double** Up = U->pointer();

    // => Work <= //

    fseek(Lf,0L,SEEK_SET);
    fseek(Rf,0L,SEEK_SET);
    for (int astart = 0; astart < na; astart+=max_a) {
        int ablock = (astart + max_a >= na ? na - astart : max_a);
        fread(Lp[0],sizeof(double),ablock*nrQ,Lf);
        if (!symm)
            fread(Rp[0],sizeof(double),ablock*nrQ,Rf);

        if (lambda) {
            for (int a = 0; a < ablock; a++) {
                for (int r = 0; r < nr; r++) {
                    C_DSCAL(nQ,(symm ? sqrt(lp[0][(a + astart)*nr + r]) : lp[0][(a + astart)*nr + r]),Rp[a*nr + r],1);
                }
            }
        }
        C_DGEMM('T','N',nQ,nQ,ablock*nr,1.0,Lp[0],nQ,Rp[0],nQ,1.0,Up[0],nQ);
    }

    return U;
}
boost::shared_ptr<Tensor> DFTSAPT::fitting(
    const std::string& name,
    boost::shared_ptr<Tensor> RT,
    boost::shared_ptr<Matrix> metric)
{
    // => Sizing <= //

    int na = RT->sizes()[0];
    int nr = RT->sizes()[1];
    int nQ = RT->sizes()[2];

    // => Target <= //

    boost::shared_ptr<Tensor> LT = DiskTensor::build(name,"a",na,"r",nr,"Q",nQ,false,false);

    // => File Pointer <= //

    FILE* Lf = LT->file_pointer();
    FILE* Rf = RT->file_pointer();

    // => Blocking <= //

    size_t nrQ = nr * (size_t) nQ;
    long int max_a = ((memory_ - nQ * (size_t) nQ) / 2L * nrQ);
    max_a = (max_a > na ? na : max_a);
    if (max_a < 1L) {
        throw PSIEXCEPTION("Not enough dynamic memory in DFTSAPT::inner");
    }

    // => Temp <= //

    boost::shared_ptr<Matrix> L(new Matrix("L",max_a*nr,nQ));
    boost::shared_ptr<Matrix> R(new Matrix("R",max_a*nr,nQ));
    double** Lp = L->pointer();
    double** Rp = R->pointer();
    double** Vp = metric->pointer();

    // => Work <= //

    fseek(Lf,0L,SEEK_SET);
    fseek(Rf,0L,SEEK_SET);
    for (int astart = 0; astart < na; astart+=max_a) {
        int ablock = (astart + max_a >= na ? na - astart : max_a);
        fread(Rp[0],sizeof(double),ablock*nrQ,Rf);
        C_DGEMM('N','N',ablock*nr,nQ,nQ,1.0,Rp[0],nQ,Vp[0],nQ,0.0,Lp[0],nQ);
        fwrite(Lp[0],sizeof(double),ablock*nrQ,Lf);
    }

    return LT;
}
void DFTSAPT::axpy(
    boost::shared_ptr<Tensor> LT,
    boost::shared_ptr<Tensor> RT,
    double alpha,
    double beta)
{
    // => Sizing <= //

    int na = LT->sizes()[0];
    int nr = LT->sizes()[1];
    int nQ = LT->sizes()[2];

    // => File Pointer <= //

    FILE* Lf = LT->file_pointer();
    FILE* Rf = RT->file_pointer();

    // => Blocking <= //

    size_t nrQ = nr * (size_t) nQ;
    long int max_a = ((memory_ - nQ * (size_t) nQ) / (2L * nrQ));
    max_a = (max_a > na ? na : max_a);
    if (max_a < 1L) {
        throw PSIEXCEPTION("Not enough dynamic memory in DFTSAPT::inner");
    }

    // => Temp <= //

    boost::shared_ptr<Matrix> L(new Matrix("L",max_a*nr,nQ));
    boost::shared_ptr<Matrix> R(new Matrix("R",max_a*nr,nQ));
    double** Lp = L->pointer();
    double** Rp = R->pointer();

    // => Work <= //

    fseek(Lf,0L,SEEK_SET);
    fseek(Rf,0L,SEEK_SET);
    for (int astart = 0; astart < na; astart+=max_a) {
        int ablock = (astart + max_a >= na ? na - astart : max_a);
        fread(Lp[0],sizeof(double),ablock*nrQ,Lf);
        fread(Rp[0],sizeof(double),ablock*nrQ,Rf);

        for (int a = 0; a < ablock; a++) {
            C_DSCAL(nr * nQ, beta, Rp[a*nr], 1);
            C_DAXPY(nr * nQ, alpha, Lp[a*nr], 1, Rp[a*nr],1);
        }

        fseek(Rf,sizeof(double) * astart * nrQ,SEEK_SET);
        fwrite(Rp[0],sizeof(double),ablock*nrQ,Rf);
    }
}

void DFTSAPT::print_trailer()
{
    energies_["delta HF,r (2)"] = 0.0;
    if (energies_["HF"] != 0.0) {
        energies_["delta HF,r (2)"] = energies_["HF"] - energies_["Elst10,r"] - energies_["Exch10"] - energies_["Ind20,r"] - energies_["Exch-Ind20,r"];
    }

    energies_["Electrostatics"] = energies_["Elst10,r"];
    energies_["Exchange"]       = energies_["Exch10"];
    energies_["Induction"]      = energies_["Ind20,r"] + energies_["Exch-Ind20,r"] + energies_["delta HF,r (2)"];
    energies_["Dispersion"]     = energies_["Disp20"] + energies_["Exch-Disp20"];
    energies_["SAPT"]           = energies_["Electrostatics"] + energies_["Exchange"] + energies_["Induction"] + energies_["Dispersion"];

    fprintf(outfile,"\n    SAPT Results  \n");
    fprintf(outfile,"  -----------------------------------------------------------------------\n");
    fprintf(outfile,"    Electrostatics     %16.8lf mH %16.8lf kcal mol^-1\n",
      energies_["Electrostatics"]*1000.0,energies_["Electrostatics"]*pc_hartree2kcalmol);
    fprintf(outfile,"      Elst10,r         %16.8lf mH %16.8lf kcal mol^-1\n\n",
      energies_["Elst10,r"]*1000.0,energies_["Elst10,r"]*pc_hartree2kcalmol);
    fprintf(outfile,"    Exchange           %16.8lf mH %16.8lf kcal mol^-1\n",
      energies_["Exchange"]*1000.0,energies_["Exchange"]*pc_hartree2kcalmol);
    fprintf(outfile,"      Exch10           %16.8lf mH %16.8lf kcal mol^-1\n",
      energies_["Exch10"]*1000.0,energies_["Exch10"]*pc_hartree2kcalmol);
    fprintf(outfile,"      Exch10(S^2)      %16.8lf mH %16.8lf kcal mol^-1\n\n",
      energies_["Exch10(S^2)"]*1000.0,energies_["Exch10(S^2)"]*pc_hartree2kcalmol);
    fprintf(outfile,"    Induction          %16.8lf mH %16.8lf kcal mol^-1\n",
      energies_["Induction"]*1000.0,energies_["Induction"]*pc_hartree2kcalmol);
    fprintf(outfile,"      Ind20,r          %16.8lf mH %16.8lf kcal mol^-1\n",
      energies_["Ind20,r"]*1000.0,energies_["Ind20,r"]*pc_hartree2kcalmol);
    fprintf(outfile,"      Exch-Ind20,r     %16.8lf mH %16.8lf kcal mol^-1\n",
      energies_["Exch-Ind20,r"]*1000.0,energies_["Exch-Ind20,r"]*pc_hartree2kcalmol);
    fprintf(outfile,"      delta HF,r (2)   %16.8lf mH %16.8lf kcal mol^-1\n\n",
      energies_["delta HF,r (2)"]*1000.0,energies_["delta HF,r (2)"]*pc_hartree2kcalmol);
    fprintf(outfile,"    Dispersion         %16.8lf mH %16.8lf kcal mol^-1\n",
      energies_["Dispersion"]*1000.0,energies_["Dispersion"]*pc_hartree2kcalmol);
    fprintf(outfile,"      Disp20           %16.8lf mH %16.8lf kcal mol^-1\n",
      energies_["Disp20"]*1000.0,energies_["Disp20"]*pc_hartree2kcalmol);
    fprintf(outfile,"      Exch-Disp20      %16.8lf mH %16.8lf kcal mol^-1\n\n",
      energies_["Exch-Disp20"]*1000.0,energies_["Exch-Disp20"]*pc_hartree2kcalmol);

    fprintf(outfile,"    Total HF           %16.8lf mH %16.8lf kcal mol^-1\n",
      energies_["HF"]*1000.0,energies_["HF"]*pc_hartree2kcalmol);
    fprintf(outfile,"    Total SAPT0        %16.8lf mH %16.8lf kcal mol^-1\n",
      energies_["SAPT"]*1000.0,energies_["SAPT"]*pc_hartree2kcalmol);
    fprintf(outfile,"\n");

    fprintf(outfile, "    Oh all the money that e'er I had, I spent it in good company.\n");
    fprintf(outfile, "\n");
}
boost::shared_ptr<Matrix> DFTSAPT::build_ind_pot(std::map<std::string, boost::shared_ptr<Matrix> >& vars)
{
    boost::shared_ptr<Matrix> Ca = vars["Cocc_A"];
    boost::shared_ptr<Matrix> Cr = vars["Cvir_A"];
    boost::shared_ptr<Matrix> V_B = vars["V_B"];
    boost::shared_ptr<Matrix> J_B = vars["J_B"];

    boost::shared_ptr<Matrix> W(V_B->clone());
    W->copy(J_B);
    W->scale(2.0);
    W->add(V_B);

    return Matrix::triplet(Ca,W,Cr,true,false,false);
}
boost::shared_ptr<Matrix> DFTSAPT::build_exch_ind_pot(std::map<std::string, boost::shared_ptr<Matrix> >& vars)
{
    boost::shared_ptr<Matrix> Ca = vars["Cocc_A"];
    boost::shared_ptr<Matrix> Cr = vars["Cvir_A"];

    boost::shared_ptr<Matrix> S = vars["S"];

    boost::shared_ptr<Matrix> D_A = vars["D_A"];
    boost::shared_ptr<Matrix> J_A = vars["J_A"];
    boost::shared_ptr<Matrix> K_A = vars["K_A"];
    boost::shared_ptr<Matrix> V_A = vars["V_A"];
    boost::shared_ptr<Matrix> D_B = vars["D_B"];
    boost::shared_ptr<Matrix> J_B = vars["J_B"];
    boost::shared_ptr<Matrix> K_B = vars["K_B"];
    boost::shared_ptr<Matrix> V_B = vars["V_B"];

    boost::shared_ptr<Matrix> J_O = vars["J_O"]; // J[D^A S D^B]
    boost::shared_ptr<Matrix> K_O = vars["K_O"]; // K[D^A S D^B]
    boost::shared_ptr<Matrix> J_P = vars["J_P"]; // J[D^B S D^A S D^B]

    boost::shared_ptr<Matrix> W(K_B->clone());
    boost::shared_ptr<Matrix> T;

    // 1
    W->copy(K_B);
    W->scale(-1.0);

    // 2
    T = Matrix::triplet(S,D_B,J_A);
    T->scale(-2.0);
    W->add(T);    

    // 3
    T->copy(K_O);
    T->scale(1.0);
    W->add(T);
    
    // 4
    T->copy(J_O);
    T->scale(-2.0);
    W->add(T);

    // 5
    T = Matrix::triplet(S,D_B,K_A);
    T->scale(1.0);
    W->add(T);

    // 6
    T = Matrix::triplet(J_B,D_B,S);
    T->scale(-2.0);
    W->add(T);

    // 7
    T = Matrix::triplet(K_B,D_B,S);
    T->scale(1.0);
    W->add(T);

    // 8
    T = Matrix::triplet(Matrix::triplet(S,D_B,J_A),D_B,S);
    T->scale(2.0);
    W->add(T);

    // 9
    T = Matrix::triplet(Matrix::triplet(J_B,D_A,S),D_B,S);
    T->scale(2.0);
    W->add(T);

    // 10
    T = Matrix::triplet(K_O,D_B,S);
    T->scale(-1.0);
    W->add(T);

    // 11
    T->copy(J_P);
    T->scale(2.0);
    W->add(T);
    
    // 12
    T = Matrix::triplet(Matrix::triplet(S,D_B,S),D_A,J_B);
    T->scale(2.0);
    W->add(T);

    // 13
    T = Matrix::triplet(S,D_B,K_O,false,false,true);
    T->scale(-1.0);
    W->add(T);

    // 14
    T = Matrix::triplet(S,D_B,V_A);
    T->scale(-1.0);
    W->add(T);    
    
    // 15
    T = Matrix::triplet(V_B,D_B,S);
    T->scale(-1.0);
    W->add(T);
    
    // 16
    T = Matrix::triplet(Matrix::triplet(S,D_B,V_A),D_B,S);
    T->scale(1.0);
    W->add(T);

    // 17
    T = Matrix::triplet(Matrix::triplet(V_B,D_A,S),D_B,S);
    T->scale(1.0);
    W->add(T);

    // 18
    T = Matrix::triplet(Matrix::triplet(S,D_B,S),D_A,V_B);
    T->scale(1.0);
    W->add(T);

    return Matrix::triplet(Ca,W,Cr,true,false,false);
}
boost::shared_ptr<Matrix> DFTSAPT::build_S(boost::shared_ptr<BasisSet> basis)
{
    boost::shared_ptr<IntegralFactory> factory(new IntegralFactory(basis));
    boost::shared_ptr<OneBodyAOInt> Sint(factory->ao_overlap());
    boost::shared_ptr<Matrix> S(new Matrix("S (AO)", basis->nbf(), basis->nbf()));
    Sint->compute(S);
    return S;
}
boost::shared_ptr<Matrix> DFTSAPT::build_V(boost::shared_ptr<BasisSet> basis)
{
    boost::shared_ptr<IntegralFactory> factory(new IntegralFactory(basis));
    boost::shared_ptr<OneBodyAOInt> Sint(factory->ao_potential());
    boost::shared_ptr<Matrix> S(new Matrix("V (AO)", basis->nbf(), basis->nbf()));
    Sint->compute(S);
    return S;
}
boost::shared_ptr<Matrix> DFTSAPT::build_Sij(boost::shared_ptr<Matrix> S)
{
    int nso = Cocc_A_->nrow();
    int nocc_A = Cocc_A_->ncol();
    int nocc_B = Cocc_B_->ncol();
    int nocc = nocc_A + nocc_B;

    boost::shared_ptr<Matrix> Sij(new Matrix("Sij (MO)", nocc, nocc));
    boost::shared_ptr<Matrix> T(new Matrix("T", nso, nocc_B));

    double** Sp = S->pointer();
    double** Tp = T->pointer();
    double** Sijp = Sij->pointer();
    double** CAp = Cocc_A_->pointer();
    double** CBp = Cocc_B_->pointer();

    C_DGEMM('N','N',nso,nocc_B,nso,1.0,Sp[0],nso,CBp[0],nocc_B,0.0,Tp[0],nocc_B);
    C_DGEMM('T','N',nocc_A,nocc_B,nso,1.0,CAp[0],nocc_A,Tp[0],nocc_B,0.0,&Sijp[0][nocc_A],nocc);

    Sij->copy_upper_to_lower();

    return Sij;
}
boost::shared_ptr<Matrix> DFTSAPT::build_Sij_n(boost::shared_ptr<Matrix> Sij)
{
    int nocc = Sij->nrow();

    boost::shared_ptr<Matrix> Sij2(new Matrix("Sij^inf (MO)", nocc, nocc));

    double** Sijp = Sij->pointer();
    double** Sij2p = Sij2->pointer();

    Sij2->copy(Sij);
    for (int i = 0; i < nocc; i++) {
        Sij2p[i][i] = 1.0;
    }

    int info;

    info = C_DPOTRF('L',nocc,Sij2p[0],nocc);
    if (info) {
        throw PSIEXCEPTION("Sij DPOTRF failed. How far up the steric wall are you?");
    }

    info = C_DPOTRI('L',nocc,Sij2p[0],nocc);
    if (info) {
        throw PSIEXCEPTION("Sij DPOTRI failed. How far up the steric wall are you?");
    }

    Sij2->copy_upper_to_lower();

    for (int i = 0; i < nocc; i++) {
        Sij2p[i][i] -= 1.0;
    }

    return Sij2;
}
std::map<std::string, boost::shared_ptr<Matrix> > DFTSAPT::build_Cbar(boost::shared_ptr<Matrix> S)
{
    std::map<std::string, boost::shared_ptr<Matrix> > Cbar;

    int nso = Cocc_A_->nrow();
    int nA = Cocc_A_->ncol();
    int nB = Cocc_B_->ncol();
    int no = nA + nB;

    double** Sp = S->pointer();
    double** CAp = Cocc_A_->pointer();
    double** CBp = Cocc_B_->pointer();
    double** Cp;

    Cbar["C_T_A"] = boost::shared_ptr<Matrix>(new Matrix("C_T_A", nso, nA));
    Cp = Cbar["C_T_A"]->pointer();
    C_DGEMM('N','N',nso,nA,nA,1.0,CAp[0],nA,&Sp[0][0],no,0.0,Cp[0],nA);

    Cbar["C_T_B"] = boost::shared_ptr<Matrix>(new Matrix("C_T_B", nso, nB));
    Cp = Cbar["C_T_B"]->pointer();
    C_DGEMM('N','N',nso,nB,nB,1.0,CBp[0],nB,&Sp[nA][nA],no,0.0,Cp[0],nB);

    Cbar["C_T_BA"] = boost::shared_ptr<Matrix>(new Matrix("C_T_BA", nso, nB));
    Cp = Cbar["C_T_BA"]->pointer();
    C_DGEMM('N','N',nso,nB,nA,1.0,CAp[0],nA,&Sp[0][nA],no,0.0,Cp[0],nB);

    Cbar["C_T_AB"] = boost::shared_ptr<Matrix>(new Matrix("C_T_AB", nso, nA));
    Cp = Cbar["C_T_AB"]->pointer();
    C_DGEMM('N','N',nso,nA,nB,1.0,CBp[0],nB,&Sp[nA][0],no,0.0,Cp[0],nA);

    return Cbar;
}
std::pair<boost::shared_ptr<Matrix>, boost::shared_ptr<Matrix> > DFTSAPT::compute_x(boost::shared_ptr<JK> jk, boost::shared_ptr<Matrix> w_B, boost::shared_ptr<Matrix> w_A)
{
    boost::shared_ptr<CPKS_SAPT> cpks(new CPKS_SAPT);

    // Effective constructor
    cpks->delta_ = cpks_delta_;
    cpks->maxiter_ = cpks_maxiter_;
    cpks->jk_ = jk;

    cpks->w_A_ = w_B; // Reversal of convention
    cpks->Cocc_A_ = Cocc_A_;
    cpks->Cvir_A_ = Cvir_A_;
    cpks->eps_occ_A_ = eps_occ_A_;
    cpks->eps_vir_A_ = eps_vir_A_;

    cpks->w_B_ = w_A; // Reversal of convention
    cpks->Cocc_B_ = Cocc_B_;
    cpks->Cvir_B_ = Cvir_B_;
    cpks->eps_occ_B_ = eps_occ_B_;
    cpks->eps_vir_B_ = eps_vir_B_;

    // Gogo CPKS
    cpks->compute_cpks();

    // Unpack
    std::pair<boost::shared_ptr<Matrix>, boost::shared_ptr<Matrix> > x_sol = make_pair(cpks->x_A_,cpks->x_B_);

    return x_sol;
}

CPKS_SAPT::CPKS_SAPT()
{
}
CPKS_SAPT::~CPKS_SAPT()
{
}
void CPKS_SAPT::compute_cpks()
{
    // Allocate
    x_A_ = boost::shared_ptr<Matrix>(w_A_->clone());
    x_B_ = boost::shared_ptr<Matrix>(w_B_->clone());
    x_A_->zero();
    x_B_->zero();

    boost::shared_ptr<Matrix> r_A(w_A_->clone());
    boost::shared_ptr<Matrix> z_A(w_A_->clone());
    boost::shared_ptr<Matrix> p_A(w_A_->clone());
    boost::shared_ptr<Matrix> r_B(w_B_->clone());
    boost::shared_ptr<Matrix> z_B(w_B_->clone());
    boost::shared_ptr<Matrix> p_B(w_B_->clone());

    // Initialize (x_0 = 0)
    r_A->copy(w_A_);
    r_B->copy(w_B_);

    preconditioner(r_A,z_A,eps_occ_A_,eps_vir_A_);
    preconditioner(r_B,z_B,eps_occ_B_,eps_vir_B_);

    // Uncoupled value
    //fprintf(outfile, "(A<-B): %24.16E\n", -2.0 * z_A->vector_dot(w_A_));
    //fprintf(outfile, "(B<-A): %24.16E\n", -2.0 * z_B->vector_dot(w_B_));

    p_A->copy(z_A);
    p_B->copy(z_B);

    double zr_old_A = z_A->vector_dot(r_A);
    double zr_old_B = z_B->vector_dot(r_B);

    double r2A = 1.0;
    double r2B = 1.0;

    double b2A = sqrt(w_A_->vector_dot(w_A_));
    double b2B = sqrt(w_B_->vector_dot(w_B_));

    fprintf(outfile, "  ==> CPKS Iterations <==\n\n");

    fprintf(outfile, "    Maxiter     = %11d\n", maxiter_);
    fprintf(outfile, "    Convergence = %11.3E\n", delta_);
    fprintf(outfile, "\n");

    time_t start;
    time_t stop;

    start = time(NULL);

    fprintf(outfile, "    -----------------------------------------\n");
    fprintf(outfile, "    %-4s %11s  %11s  %10s\n", "Iter", "Monomer A", "Monomer B", "Time [s]");
    fprintf(outfile, "    -----------------------------------------\n");
    fflush(outfile);

    int iter;
    for (iter = 0; iter < maxiter_; iter++) {

        std::map<std::string, boost::shared_ptr<Matrix> > b;
        if (r2A > delta_) {
            b["A"] = p_A;
        }
        if (r2B > delta_) {
            b["B"] = p_B;
        }

        std::map<std::string, boost::shared_ptr<Matrix> > s =
            product(b);

        if (r2A > delta_) {
            boost::shared_ptr<Matrix> s_A = s["A"];
            double alpha = r_A->vector_dot(z_A) / p_A->vector_dot(s_A);
            if (alpha < 0.0) {
                throw PSIEXCEPTION("Monomer A: A Matrix is not SPD");
            }
            int no = x_A_->nrow();
            int nv = x_A_->ncol();
            double** xp = x_A_->pointer();
            double** rp = r_A->pointer();
            double** pp = p_A->pointer();
            double** sp = s_A->pointer();
            C_DAXPY(no*nv, alpha,pp[0],1,xp[0],1);
            C_DAXPY(no*nv,-alpha,sp[0],1,rp[0],1);
            r2A = sqrt(C_DDOT(no*nv,rp[0],1,rp[0],1)) / b2A;
        }

        if (r2B > delta_) {
            boost::shared_ptr<Matrix> s_B = s["B"];
            double alpha = r_B->vector_dot(z_B) / p_B->vector_dot(s_B);
            if (alpha < 0.0) {
                throw PSIEXCEPTION("Monomer B: A Matrix is not SPD");
            }
            int no = x_B_->nrow();
            int nv = x_B_->ncol();
            double** xp = x_B_->pointer();
            double** rp = r_B->pointer();
            double** pp = p_B->pointer();
            double** sp = s_B->pointer();
            C_DAXPY(no*nv, alpha,pp[0],1,xp[0],1);
            C_DAXPY(no*nv,-alpha,sp[0],1,rp[0],1);
            r2B = sqrt(C_DDOT(no*nv,rp[0],1,rp[0],1)) / b2B;
        }

        stop = time(NULL);
        fprintf(outfile, "    %-4d %11.3E%1s %11.3E%1s %10ld\n", iter+1,
            r2A, (r2A < delta_ ? "*" : " "),
            r2B, (r2B < delta_ ? "*" : " "),
            stop-start
            );
        fflush(outfile);

        if (r2A <= delta_ && r2B <= delta_) {
            break;
        }

        if (r2A > delta_) {
            preconditioner(r_A,z_A,eps_occ_A_,eps_vir_A_);
            double zr_new = z_A->vector_dot(r_A);
            double beta = zr_new / zr_old_A;
            zr_old_A = zr_new;
            int no = x_A_->nrow();
            int nv = x_A_->ncol();
            double** pp = p_A->pointer();
            double** zp = z_A->pointer();
            C_DSCAL(no*nv,beta,pp[0],1);
            C_DAXPY(no*nv,1.0,zp[0],1,pp[0],1);
        }

        if (r2B > delta_) {
            preconditioner(r_B,z_B,eps_occ_B_,eps_vir_B_);
            double zr_new = z_B->vector_dot(r_B);
            double beta = zr_new / zr_old_B;
            zr_old_B = zr_new;
            int no = x_B_->nrow();
            int nv = x_B_->ncol();
            double** pp = p_B->pointer();
            double** zp = z_B->pointer();
            C_DSCAL(no*nv,beta,pp[0],1);
            C_DAXPY(no*nv,1.0,zp[0],1,pp[0],1);
        }
    }

    fprintf(outfile, "    -----------------------------------------\n");
    fprintf(outfile, "\n");
    fflush(outfile);

    if (iter == maxiter_)
        throw PSIEXCEPTION("CPKS did not converge.");
}
void CPKS_SAPT::preconditioner(boost::shared_ptr<Matrix> r,
                               boost::shared_ptr<Matrix> z,
                               boost::shared_ptr<Vector> o,
                               boost::shared_ptr<Vector> v)
{
    int no = o->dim();
    int nv = v->dim();

    double** rp = r->pointer();
    double** zp = z->pointer();

    double* op = o->pointer();
    double* vp = v->pointer();

    for (int i = 0; i < no; i++) {
        for (int a = 0; a < nv; a++) {
            zp[i][a] = rp[i][a] / (vp[a] - op[i]);
        }
    }
}
std::map<std::string, boost::shared_ptr<Matrix> > CPKS_SAPT::product(std::map<std::string, boost::shared_ptr<Matrix> > b)
{
    std::map<std::string, boost::shared_ptr<Matrix> > s;

    bool do_A = b.count("A");
    bool do_B = b.count("B");

    std::vector<SharedMatrix>& Cl = jk_->C_left();
    std::vector<SharedMatrix>& Cr = jk_->C_right();
    Cl.clear();
    Cr.clear();

    if (do_A) {
        Cl.push_back(Cocc_A_);
        int no = b["A"]->nrow();
        int nv = b["A"]->ncol();
        int nso = Cvir_A_->nrow();
        double** Cp = Cvir_A_->pointer();
        double** bp = b["A"]->pointer();
        boost::shared_ptr<Matrix> T(new Matrix("T",nso,no));
        double** Tp = T->pointer();
        C_DGEMM('N','T',nso,no,nv,1.0,Cp[0],nv,bp[0],nv,0.0,Tp[0],no);
        Cr.push_back(T);
    }

    if (do_B) {
        Cl.push_back(Cocc_B_);
        int no = b["B"]->nrow();
        int nv = b["B"]->ncol();
        int nso = Cvir_B_->nrow();
        double** Cp = Cvir_B_->pointer();
        double** bp = b["B"]->pointer();
        boost::shared_ptr<Matrix> T(new Matrix("T",nso,no));
        double** Tp = T->pointer();
        C_DGEMM('N','T',nso,no,nv,1.0,Cp[0],nv,bp[0],nv,0.0,Tp[0],no);
        Cr.push_back(T);
    }

    jk_->compute();

    const std::vector<SharedMatrix>& J = jk_->J();
    const std::vector<SharedMatrix>& K = jk_->K();

    int indA = 0;
    int indB = (do_A ? 1 : 0);

    if (do_A) {
        boost::shared_ptr<Matrix> Jv = J[indA];
        boost::shared_ptr<Matrix> Kv = K[indA];
        Jv->scale(4.0);
        Jv->subtract(Kv);
        Jv->subtract(Kv->transpose());

        int no = b["A"]->nrow();
        int nv = b["A"]->ncol();
        int nso = Cvir_A_->nrow();
        boost::shared_ptr<Matrix> T(new Matrix("T", no, nso));
        s["A"] = boost::shared_ptr<Matrix>(new Matrix("S", no, nv));
        double** Cop = Cocc_A_->pointer();
        double** Cvp = Cvir_A_->pointer();
        double** Jp = Jv->pointer();
        double** Tp = T->pointer();
        double** Sp = s["A"]->pointer();
        C_DGEMM('T','N',no,nso,nso,1.0,Cop[0],no,Jp[0],nso,0.0,Tp[0],nso);
        C_DGEMM('N','N',no,nv,nso,1.0,Tp[0],nso,Cvp[0],nv,0.0,Sp[0],nv);

        double** bp = b["A"]->pointer();
        double* op = eps_occ_A_->pointer();
        double* vp = eps_vir_A_->pointer();
        for (int i = 0; i < no; i++) {
            for (int a = 0; a < nv; a++) {
                Sp[i][a] += bp[i][a] * (vp[a] - op[i]);
            }
        }
    }

    if (do_B) {
        boost::shared_ptr<Matrix> Jv = J[indB];
        boost::shared_ptr<Matrix> Kv = K[indB];
        Jv->scale(4.0);
        Jv->subtract(Kv);
        Jv->subtract(Kv->transpose());

        int no = b["B"]->nrow();
        int nv = b["B"]->ncol();
        int nso = Cvir_B_->nrow();
        boost::shared_ptr<Matrix> T(new Matrix("T", no, nso));
        s["B"] = boost::shared_ptr<Matrix>(new Matrix("S", no, nv));
        double** Cop = Cocc_B_->pointer();
        double** Cvp = Cvir_B_->pointer();
        double** Jp = Jv->pointer();
        double** Tp = T->pointer();
        double** Sp = s["B"]->pointer();
        C_DGEMM('T','N',no,nso,nso,1.0,Cop[0],no,Jp[0],nso,0.0,Tp[0],nso);
        C_DGEMM('N','N',no,nv,nso,1.0,Tp[0],nso,Cvp[0],nv,0.0,Sp[0],nv);

        double** bp = b["B"]->pointer();
        double* op = eps_occ_B_->pointer();
        double* vp = eps_vir_B_->pointer();
        for (int i = 0; i < no; i++) {
            for (int a = 0; a < nv; a++) {
                Sp[i][a] += bp[i][a] * (vp[a] - op[i]);
            }
        }
    }

    return s;
}

GaussChebyshev::GaussChebyshev(int npoint, double scale) :
    npoint_(npoint), scale_(scale)
{
}
GaussChebyshev::~GaussChebyshev()
{
}
void GaussChebyshev::print_header()
{
    fprintf(outfile,"  ==> Gauss-Chebyshev Quadrature <==\n\n");
    fprintf(outfile,"    Points = %11d\n", npoint_);
    fprintf(outfile,"    Scale  = %11.3E\n",scale_);
    fprintf(outfile,"\n");
    fflush(outfile);
}
void GaussChebyshev::compute()
{
    // Compute Becke-style mapping
    // (Seems to span the space better)
    nodes_.resize(npoint_);
    weights_.resize(npoint_);
    double x,temp;
    double INVLN2 = 1.0/log(2.0);
    for (int tau = 1; tau<=npoint_; tau++) {
        x = cos(tau/(npoint_+1.0)*M_PI);
        nodes_[tau-1] = scale_*(1.0-x)/(1.0+x);
        temp = sin(tau/(npoint_+1.0)*M_PI);
        weights_[tau-1] = 2.0*M_PI/(npoint_+1)*temp*temp*scale_/((1.0+x)*(1.0+x)*
          sqrt(1.0-x*x));
    }
}

}}
