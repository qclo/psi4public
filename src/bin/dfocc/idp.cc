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

#include "defines.h"
#include "dfocc.h"

using namespace psi;
using namespace std;

namespace psi{ namespace dfoccwave{

void DFOCC::idp()
{
     int dim, block;

if (reference_ == "RESTRICTED") {
    // Form IDPs
    nidpA=0;

    // All V-O 
    if (nfrzc == 0 && nfrzv == 0) nidpA += nvirA * noccA; 

    // All V-O, AOCC-FC 
    else if (nfrzc > 0 && nfrzv == 0) {
             nidpA += nvirA * noccA; 
             nidpA += naoccA * nfrzc; 
    }

    // All V-O, AOCC-FC, FV-AVIR 
    else if (nfrzc > 0 && nfrzv > 0) {
             nidpA += nvirA * noccA; 
             nidpA += naoccA * nfrzc; 
             nidpA += navirA * nfrzv; 
    }

    fprintf(outfile,"\n\tNumber of independent-pairs: %3d\n", nidpA);
    fflush(outfile);  
    
    if (nidpA > 0) {
      idp_returnA = 1;
      wogA = SharedTensor1d(new Tensor1d("Alpha MO grad vector", nidpA));
      kappaA = SharedTensor1d(new Tensor1d("Alpha orb rot params vector of current step", nidpA));
      kappa_newA = SharedTensor1d(new Tensor1d("Alpha new orb rot params vector of current step", nidpA));
      kappa_barA = SharedTensor1d(new Tensor1d("Alpha orb rot params vector with respect to scf MOs", nidpA));
      wog_intA = SharedTensor1d(new Tensor1d("Alpha Interpolated MO grad vector", nidpA));
      idprowA = SharedTensor1i(new Tensor1i("Alpha IDP Row", nidpA));
      idpcolA = SharedTensor1i(new Tensor1i("Alpha IDP Col", nidpA));

      // set idpA 
      dim=0;

      // V-O 
      if (nfrzc == 0 && nfrzv == 0) { 
          for(int a = 0; a < nvirA; a++){
	      for(int i = 0; i < noccA; i++){
	          idprowA->set(dim, a + noccA);
	          idpcolA->set(dim, i);
	          dim++;  
	      }
          }
      }

      // All V-O, AOCC-FC 
      else if (nfrzc > 0 && nfrzv == 0) {
          // AOCC-FC
	  for(int i = 0; i < naoccA; i++){
	      for(int j = 0; j < nfrzc; j++){
	          idprowA->set(dim, i + nfrzc);
	          idpcolA->set(dim, j);
	          dim++;  
	      }
          }

          // V-O
          for(int a = 0; a < nvirA; a++){
	      for(int i = 0; i < noccA; i++){
	          idprowA->set(dim, a + noccA);
	          idpcolA->set(dim, i);
	          dim++;  
	      }
          }
      }

      // All V-O, AOCC-FC, FV-AVIR 
      else if (nfrzc > 0 && nfrzv > 0) {
          // AOCC-FC
	  for(int i = 0; i < naoccA; i++){
	      for(int j = 0; j < nfrzc; j++){
	          idprowA->set(dim, i + nfrzc);
	          idpcolA->set(dim, j);
	          dim++;  
	      }
          }

          // V-O
          for(int a = 0; a < nvirA; a++){
	      for(int i = 0; i < noccA; i++){
	          idprowA->set(dim, a + noccA);
	          idpcolA->set(dim, i);
	          dim++;  
	      }
          }

          // FV-AVIR
          for(int a = 0; a < nfrzv; a++){
              for(int b = 0; b < nvirA; b++){
	          idprowA->set(dim, a + npop);
	          idpcolA->set(dim, b + noccA);
	          dim++;  
	      }
          }
      }

      if (print_ > 2){
         for(int i = 0; i < nidpA; i++){
             fprintf(outfile,"\ti, idprowA, idpcolA: %3d %3d %3d\n", i, idprowA->get(i), idpcolA->get(i));
	     fflush(outfile);
         }
      }
    }// end if nidpA != 0

    else if (nidpA == 0) {
            fprintf(outfile,"\tThere is not any non-redundant orbital rotation pair! \n");
            tstop();
            exit(EXIT_SUCCESS);
    }
     
}// end if (reference_ == "RESTRICTED") 

else if (reference_ == "UNRESTRICTED") {
    // Form IDPs
    nidpA=0;
    nidpB=0;

    // All V-O 
    if (nfrzc == 0 && nfrzv == 0) { 
        nidpA += nvirA * noccA; 
        nidpB += nvirB * noccB; 
    }

    // All V-O, AOCC-FC 
    else if (nfrzc > 0 && nfrzv == 0) {
             nidpA += nvirA * noccA; 
             nidpA += naoccA * nfrzc; 
             nidpB += nvirB * noccB; 
             nidpB += naoccB * nfrzc; 
    }

    // All V-O, AOCC-FC, FV-AVIR 
    else if (nfrzc > 0 && nfrzv > 0) {
             nidpA += nvirA * noccA; 
             nidpA += naoccA * nfrzc; 
             nidpA += navirA * nfrzv; 
             nidpB += nvirB * noccB; 
             nidpB += naoccB * nfrzc; 
             nidpB += navirB * nfrzv; 
    }

    fprintf(outfile,"\n\tNumber of alpha independent-pairs:%3d\n", nidpA);
    fprintf(outfile,"\tNumber of beta independent-pairs :%3d\n", nidpB);
    fflush(outfile);  

    if (nidpA == 0 && nidpB == 0) {
        fprintf(outfile,"\tThere is not any non-redundant orbital rotation pair! \n");
        tstop();
        exit(EXIT_SUCCESS);
    }
    
     if (nidpA > 0) {
      idp_returnA = 1;
      wogA = SharedTensor1d(new Tensor1d("Alpha MO grad vector", nidpA));
      kappaA = SharedTensor1d(new Tensor1d("Alpha orb rot params vector of current step", nidpA));
      kappa_newA = SharedTensor1d(new Tensor1d("Alpha new orb rot params vector of current step", nidpA));
      kappa_barA = SharedTensor1d(new Tensor1d("Alpha orb rot params vector with respect to scf MOs", nidpA));
      wog_intA = SharedTensor1d(new Tensor1d("Alpha Interpolated MO grad vector", nidpA));
      idprowA = SharedTensor1i(new Tensor1i("Alpha IDP Row", nidpA));
      idpcolA = SharedTensor1i(new Tensor1i("Alpha IDP Col", nidpA));

      // set idpA 
      dim=0;

      // V-O 
      if (nfrzc == 0 && nfrzv == 0) { 
          for(int a = 0; a < nvirA; a++){
	      for(int i = 0; i < noccA; i++){
	          idprowA->set(dim, a + noccA);
	          idpcolA->set(dim, i);
	          dim++;  
	      }
          }
      }

      // All V-O, AOCC-FC 
      else if (nfrzc > 0 && nfrzv == 0) {
          // AOCC-FC
	  for(int i = 0; i < naoccA; i++){
	      for(int j = 0; j < nfrzc; j++){
	          idprowA->set(dim, i + nfrzc);
	          idpcolA->set(dim, j);
	          dim++;  
	      }
          }

          // V-O
          for(int a = 0; a < nvirA; a++){
	      for(int i = 0; i < noccA; i++){
	          idprowA->set(dim, a + noccA);
	          idpcolA->set(dim, i);
	          dim++;  
	      }
          }
      }

      // All V-O, AOCC-FC, FV-AVIR 
      else if (nfrzc > 0 && nfrzv > 0) {
          // AOCC-FC
	  for(int i = 0; i < naoccA; i++){
	      for(int j = 0; j < nfrzc; j++){
	          idprowA->set(dim, i + nfrzc);
	          idpcolA->set(dim, j);
	          dim++;  
	      }
          }

          // V-O
          for(int a = 0; a < nvirA; a++){
	      for(int i = 0; i < noccA; i++){
	          idprowA->set(dim, a + noccA);
	          idpcolA->set(dim, i);
	          dim++;  
	      }
          }

          // FV-AVIR
          for(int a = 0; a < nfrzv; a++){
              for(int b = 0; b < nvirA; b++){
	          idprowA->set(dim, a + npop);
	          idpcolA->set(dim, b + noccA);
	          dim++;  
	      }
          }
      }

      if (print_ > 2){
         for(int i = 0; i < nidpA; i++){
             fprintf(outfile,"\n\t i, idprowA, idpcolA: %3d %3d %3d\n", i, idprowA->get(i), idpcolA->get(i));
	     fflush(outfile);
         }
      }
    }// end if nidpA != 0
    
     if (nidpB > 0) {
      idp_returnB = 1;
      wogB = SharedTensor1d(new Tensor1d("Beta MO grad vector", nidpB));
      kappaB = SharedTensor1d(new Tensor1d("Beta orb rot params vector of current step", nidpB));
      kappa_newB = SharedTensor1d(new Tensor1d("Beta new orb rot params vector of current step", nidpB));
      kappa_barB = SharedTensor1d(new Tensor1d("Beta orb rot params vector with respect to scf MOs", nidpB));
      wog_intB = SharedTensor1d(new Tensor1d("Beta Interpolated MO grad vector", nidpB));
      idprowB = SharedTensor1i(new Tensor1i("Beta IDP Row", nidpB));
      idpcolB = SharedTensor1i(new Tensor1i("Beta IDP Col", nidpB));

      // set idpB 
      dim=0;

      // V-O 
      if (nfrzc == 0 && nfrzv == 0) { 
          for(int a = 0; a < nvirB; a++){
	      for(int i = 0; i < noccB; i++){
	          idprowB->set(dim, a + noccB);
	          idpcolB->set(dim, i);
	          dim++;  
	      }
          }
      }

      // All V-O, AOCC-FC 
      else if (nfrzc > 0 && nfrzv == 0) {
          // AOCC-FC
	  for(int i = 0; i < naoccB; i++){
	      for(int j = 0; j < nfrzc; j++){
	          idprowB->set(dim, i + nfrzc);
	          idpcolB->set(dim, j);
	          dim++;  
	      }
          }

          // V-O
          for(int a = 0; a < nvirB; a++){
	      for(int i = 0; i < noccB; i++){
	          idprowB->set(dim, a + noccB);
	          idpcolB->set(dim, i);
	          dim++;  
	      }
          }
      }

      // All V-O, AOCC-FC, FV-AVIR 
      else if (nfrzc > 0 && nfrzv > 0) {
          // AOCC-FC
	  for(int i = 0; i < naoccB; i++){
	      for(int j = 0; j < nfrzc; j++){
	          idprowB->set(dim, i + nfrzc);
	          idpcolB->set(dim, j);
	          dim++;  
	      }
          }

          // V-O
          for(int a = 0; a < nvirB; a++){
	      for(int i = 0; i < noccB; i++){
	          idprowB->set(dim, a + noccB);
	          idpcolB->set(dim, i);
	          dim++;  
	      }
          }

          // FV-AVIR
          for(int a = 0; a < nfrzv; a++){
              for(int b = 0; b < nvirB; b++){
	          idprowB->set(dim, a + npop);
	          idpcolB->set(dim, b + noccB);
	          dim++;  
	      }
          }
      }

      if (print_ > 2){
         for(int i = 0; i < nidpB; i++){
             fprintf(outfile,"\n\t i, idprowB, idpcolB: %3d %3d %3d\n", i, idprowB->get(i), idpcolB->get(i));
	     fflush(outfile);
         }
      }
    }// end if nidpB != 0
      
}// end if (reference_ == "UNRESTRICTED") 

}// end of main
}} // End Namespaces


