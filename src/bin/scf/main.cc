#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <sstream>

#include <psifiles.h>
#include <libciomr/libciomr.h>
#include <libpsio/psio.h>
#include <libchkpt/chkpt.h>
#include <libpsio/psio.hpp>
#include <libchkpt/chkpt.hpp>
#include <libipv1/ip_lib.h>
#include <libiwl/iwl.h>
#include <libqt/qt.h>

#include <libmints/mints.h>
#include "hfenergy.h"

#include <psi4-dec.h>

namespace psi { namespace scf {

std::string to_string(const int val);   // In matrix.cpp

PsiReturnType scf(Options & options, int /* argc */, char * /* argv */[])
{
    tstart();
    
    Wavefunction::initialize_singletons();
    
    shared_ptr<PSIO> psio(new PSIO);
    psiopp_ipv1_config(psio);
    
    shared_ptr<Chkpt> chkpt(new Chkpt(psio, PSIO_OPEN_OLD));
    
    // Initialize the psi3 timer library.
    timer_init();

    // Compute the Hartree-Fock energy
    HFEnergy hf(options, psio, chkpt);
    /* double hf_energy = */ hf.compute_energy();
    
    // Shut down psi. 
    timer_done();

    tstop();
    
    return Success;
}

}}
