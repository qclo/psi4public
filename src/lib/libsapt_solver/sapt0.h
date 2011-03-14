#ifndef SAPT0_H
#define SAPT0_H

#include "sapt.h"

using namespace psi;

namespace psi { namespace sapt {

struct SAPTDFInts;
struct Iterator;

class SAPT0 : public SAPT {
private:
  void print_header();
  void print_results();

  void df_integrals();
  void w_integrals();

  SAPTDFInts set_A_AA();
  SAPTDFInts set_B_BB();
  SAPTDFInts set_A_AR();
  SAPTDFInts set_B_BS();
  SAPTDFInts set_A_AB();
  SAPTDFInts set_B_AB();
  SAPTDFInts set_A_RB();
  SAPTDFInts set_B_RB();
  SAPTDFInts set_A_AS();
  SAPTDFInts set_B_AS();
  SAPTDFInts set_C_AA();
  SAPTDFInts set_C_AR();
  SAPTDFInts set_C_RR();
  SAPTDFInts set_C_BB();
  SAPTDFInts set_C_BS();
  SAPTDFInts set_C_SS();

  Iterator get_iterator(long int, SAPTDFInts*);
  Iterator set_iterator(int, SAPTDFInts*);

  Iterator get_iterator(long int, SAPTDFInts*, SAPTDFInts*);
  Iterator set_iterator(int, SAPTDFInts*, SAPTDFInts*);
 
  void read_all(SAPTDFInts*);
  void read_block(Iterator *, SAPTDFInts *);
  void read_block(Iterator *, SAPTDFInts *, SAPTDFInts *);

  void ind20rA_B();
  void ind20rB_A();

protected:
  shared_ptr<SAPTLaplaceDenominator> denom_;

  int maxiter_;
  double e_conv_;
  double d_conv_;

  double e_elst10_;
  double e_exch10_;
  double e_exch10_s2_;
  double e_ind20_;
  double e_exch_ind20_;
  double e_disp20_;
  double e_exch_disp20_;
  double e_sapt0_;

  double **wBAR_;
  double **wABS_;

public:
  SAPT0(Options& options, shared_ptr<PSIO> psio, shared_ptr<Chkpt> chkpt);
  virtual ~SAPT0();

  virtual double compute_energy();

  void elst10();
  void exch10();
  void exch10_s2();
  void ind20();
  void ind20r();
  void exch_ind20A_B();
  void exch_ind20B_A();
  void disp20();

};

struct SAPTDFInts {

  bool dress_;
  bool active_;

  int i_length_;
  int j_length_;
  int ij_length_;
  int i_start_;
  int j_start_;

  double **B_p_;
  double **B_d_;

  int filenum_;
  char *label_;

  psio_address next_DF_;

  SAPTDFInts() { next_DF_ = PSIO_ZERO; B_p_ = NULL; B_d_ = NULL; };
  ~SAPTDFInts() { 
    if (B_p_ != NULL) free_block(B_p_); 
    if (B_d_ != NULL) free_block(B_d_); };
  void rewind() { next_DF_ = PSIO_ZERO; };
  void clear() { free_block(B_p_); B_p_ = NULL; next_DF_ = PSIO_ZERO; };
  void done() { 
    free_block(B_p_); if (dress_) free_block(B_d_);
    B_p_ = NULL; B_d_ = NULL; };
};

struct Iterator {

  int num_blocks;
  int* block_size;

  int curr_block;
  long int curr_size;

  ~Iterator() { free(block_size); };
  void rewind() { curr_block = 1; curr_size = 0; };
};

}}

#endif
