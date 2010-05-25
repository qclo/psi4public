#ifndef LIBSCF_NULL_FUNCTIONAL
#define LIBSCF_NULL_FUNCTIONAL

/*
 *  NULL_Functional.h
 *  Definition of class NULL_Functional for use in KS-DFT
 *
 *  Created by Robert Parrish on 02/24/10.
 *
 */

#include <libmints/properties.h>
#include <libscf_solver/functional.h>
#include <stdlib.h>
#include <string>
using namespace psi;

namespace psi { namespace scf {
/*! \ingroup SCF */
//! NULL_Functional definition 
class NULL_Functional: public Functional {
public:
	/** Constructor
	* Allocates required arrays, returns object
	*/
	NULL_Functional(int); 
	/** Destructor
	* Frees arrays
	*/
	~NULL_Functional();
	/** Is this functional a GGA?
	* @return false, LSDA
	*/
	bool isGGA() {return false;}
	/** Does this functional have exact exchange?
	* @return false, no exact exchange
	*/
	bool hasExactExchange() {return false;}
	/** Exact exchange coefficient (0.0 if no exact exchange)
	* @return 0.0
	*/
	double getExactExchangeCoefficient() {return 0.0;}
	/** Exact Coulomb coefficient (1.0 if usual)
	* @return 1.0
	*/
	double getExactCoulombCoefficient() {return 1.0;}
	/** Does this functional need electron density?
	* @return true, needs density
	*/
	bool needsDensity() {return true; }
	/** Does this functional need electron density gradients?
	* @return true if so, false otherwise
	*/
	bool needsDensityGradient() {return false; }
	/** Does this functional need electron density jacobians?
	* @return true if so, false otherwise
	*/
	bool needsDensityHessian() {return false; }
	/** Does this functional need electron density laplacians?
	* @return true if so, false otherwise
	*/
	bool needsDensityLaplacian() {return false; }
	/** Does this functional need kinetic energy density?
	* @return true if so, false otherwise
	*/
	bool needsKEDensity() {return false; }
	/** Functional Value at specified point properties
        * for RKS type SCF 
	* 
	*/
	void computeFunctionalRKS(shared_ptr<Properties> prop);
	/** Functional Value at specified point properties
        * for UKS type SCF 
	* 
	*/
	void computeFunctionalUKS(shared_ptr<Properties> prop);
	/** Functional name
	* @return functional or alias name ie: 'B3LYP'
	*/
	string getName();
	/** Functional description
	* @return description of functional or especially
	* substituent functionals of aliases
	*/
	string getDescription();
	/** Functional citation
	* @return citation of functional
	*/
	string getCitation();	
};

}}
#endif