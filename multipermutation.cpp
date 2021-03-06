// Copyright 2012, Olav Stetter
// 
// This file is part of TE-Causality.
// 
// TE-Causality is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// TE-Causality is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with TE-Causality. If not, see <http://www.gnu.org/licenses/>.

#include "multipermutation.h"


// init
MultiPermutation::MultiPermutation(const gsl_vector_int* ps) {
  if(ps->size < 1) {
    std::cout <<"error: init vector for MultiPermutation has zero length!"<<std::endl;
    exit(1);
  }
  permutation_elements = gsl_vector_int_alloc(ps->size);
  gsl_vector_int_memcpy(permutation_elements,ps);
  
  temp_access_vector = gsl_vector_int_alloc(required_length_of_access_vector());
  gsl_vector_int_set_all(temp_access_vector, -1);
  
  int req_length = required_length_of_reduced_access_vector();
  if( req_length < 1 ) {
    std::cout <<"Error: Init vector for MultiPermutation effectively consists of single number!"<<std::endl;
    exit(1);  
  } else {
    temp_access_vector_reduced = gsl_vector_int_alloc(required_length_of_reduced_access_vector());
    set_reduced_temp_vector_to_reduced_upper_bound_of_permutation_values();
    mem = new MultiDimArrayLong(temp_access_vector_reduced);
  
    mem->clear();
  }
}

// destructor
MultiPermutation::~MultiPermutation() {
  if( mem != NULL ) { delete mem; }
  gsl_vector_int_free(permutation_elements);
  gsl_vector_int_free(temp_access_vector);
  gsl_vector_int_free(temp_access_vector_reduced);
}

void MultiPermutation::set_temp_vector_to_upper_bound_of_permutation_values() {
  int element;
  int c = 0;
  for(int i=0; i<permutation_elements->size; i++) {
    element = gsl_vector_int_get(permutation_elements,i);
    for(int j=0; j<element; j++) { gsl_vector_int_set(temp_access_vector,c++,element); }
  }
}

int MultiPermutation::required_length_of_access_vector() const {
  int total = 0;
  for(int i=0; i<permutation_elements->size; i++) {
    total += gsl_vector_int_get(permutation_elements,i);
  }
  return total;
}

int MultiPermutation::required_length_of_reduced_access_vector() const {
  int total = 0;
  int element;
  for(int i=0; i<permutation_elements->size; i++) {
    element = gsl_vector_int_get(permutation_elements,i);
    // reduced, 1 permutation only is ignored.
    if( element > 1 ) {
      total += element - 1;
    }
  }
  return total;
}

long MultiPermutation::get(const gsl_vector_int* access, bool assume_validity_of_access) {
  if(!assume_validity_of_access && !test_validity_of_given_access_vector(access)) {
    std::cout <<"error: access vector for MultiPermutation#get is invalid!"<<std::endl;
    exit(1);
  }

  set_reduced_temp_vector_to_reduced_access_vector(access);
  return mem->get(temp_access_vector_reduced);
}

void MultiPermutation::set(const gsl_vector_int* access, long value, bool assume_validity_of_access) {
  if(!assume_validity_of_access && !test_validity_of_given_access_vector(access)) {
    std::cout <<"error: access vector for MultiPermutation#set is invalid!"<<std::endl;
    exit(1);
  }

  set_reduced_temp_vector_to_reduced_access_vector(access);
  return mem->set(temp_access_vector_reduced, value);
}

void MultiPermutation::inc(const gsl_vector_int* access, long value, bool assume_validity_of_access) {
  if(!assume_validity_of_access && !test_validity_of_given_access_vector(access)) {
    std::cout <<"error: access vector for MultiPermutation#inc/dec is invalid!"<<std::endl;
    exit(1);
  }

  set_reduced_temp_vector_to_reduced_access_vector(access);
  mem->inc(temp_access_vector_reduced, value);
}

void MultiPermutation::dec(const gsl_vector_int* access, long value, bool assume_validity_of_access) {
  inc(access, -value, assume_validity_of_access);
}

void MultiPermutation::clear() {
  // This is not implemented via MultiDimArrayLong#set_all because all the entries which
  // should be zero would also be changed, thus returning a wrong result when calling the
  // function MultiPermutation#total.
  mem->clear();
}

long MultiPermutation::total() {
  return mem->total();
}

bool MultiPermutation::test_validity_of_given_access_vector(const gsl_vector_int* access) {
  // test if length is sufficient (we allow more entries which will be ignored)
  if( access->size < required_length_of_access_vector() ) {
    // std::cout <<"DEBUG: Length of access vector ("<<access->size<<") is too small!"<<std::endl;
    return false;
  }
  
  // test if individual permutations are valid
  gsl_vector_int_set_zero(temp_access_vector);
  int offset = 0;
  int local_limit, access_element;
  for(int i=0; i<permutation_elements->size; i++) {
    local_limit = gsl_vector_int_get(permutation_elements, i);
    for(int j=0; j<local_limit; j++) {
      access_element = gsl_vector_int_get(access, offset+j);
      if( access_element < 0 ) {
        // std::cout <<"DEBUG: An element of the access vector is negative ("<<access_element<<")!"<<std::endl;
        return false;
      }
      if( access_element >= local_limit ) {
        // std::cout <<"DEBUG: An element of the access vector is too large ("<<access_element<<")!"<<std::endl;
        return false;
      }
      if( gsl_vector_int_get(temp_access_vector, access_element+offset) == 0 ) {
        gsl_vector_int_set(temp_access_vector, access_element+offset, 1);
      } else {
        // std::cout <<"DEBUG: Two elements of this part of the multi-permutation are identical!"<<std::endl;
        return false;
      }
    }
    offset += local_limit;
  }
  return true;
}

void compute_permutation(const gsl_vector* vector, gsl_vector_int* resulting_ranks, int start_index) {
  gsl_permutation* vector_sorting = gsl_permutation_alloc(vector->size);
  compute_permutation(vector, vector_sorting, resulting_ranks, start_index);
  gsl_permutation_free(vector_sorting);
}
void compute_permutation(const gsl_vector* vector, gsl_permutation* vector_sorting, gsl_vector_int* resulting_ranks, int start_index) {
  if(resulting_ranks->size < vector->size+start_index) {
    std::cout <<"Error in compute_permutation: Incompatible vector lengths."<<std::endl;
    exit(1);
  }

  // compute ranks (taken from GSL reference, sec. 11.4)
  gsl_sort_vector_index(vector_sorting,vector);
  int pi;
  for(int i = 0; i < vector->size; i++) {
    pi = vector_sorting->data[i];
    resulting_ranks->data[pi+start_index] = i;
  }
  
  // DEBUG OUTPUT:
  // for(int i = 0; i < maximum_rank; i++) {
  //   std::cout <<"bin #"<<i<<": value "<<gsl_vector_get(vec_Full_double,i)<<" -> index "<<gsl_permutation_get(local_rank_vector,i)<<" -> rank "<<gsl_vector_int_get(vec_Full,i)<<endl;
  // }
  // SimplePrintFullIterator();
  // exit(0);
}

void MultiPermutation::compute_permutations(const gsl_vector* input_vector, gsl_vector_int* resulting_ranks) {
  if(resulting_ranks->size < input_vector->size) {
    std::cout <<"Error in compute_permutation: Incompatible vector lengths."<<std::endl;
    exit(1);
  }

  int offset = 0;
  int local_limit;
  for(int i=0; i<permutation_elements->size; i++) {
    local_limit = gsl_vector_int_get(permutation_elements, i);
    compute_single_permutation_element(input_vector, offset, offset+local_limit-1, resulting_ranks, offset);
    offset += local_limit;
  }
}

void MultiPermutation::compute_single_permutation_element(const gsl_vector* input_vector, int in_start, int in_end, gsl_vector_int* resulting_ranks, int res_start) {
  int length = in_end - in_start + 1;
  gsl_permutation* vector_sorting = gsl_permutation_alloc(length);
  
  // gsl_vector_view input_view;
  // input_view = gsl_vector_subvector(input_vector, in_start, length);
  const gsl_vector_const_view input_view = gsl_vector_const_subvector(input_vector, in_start, length);
  
  gsl_sort_vector_index(vector_sorting, &input_view.vector);
  int pi;
  for(int i = 0; i < length; i++) {
    pi = gsl_permutation_get(vector_sorting, i);
    gsl_vector_int_set(resulting_ranks, pi+res_start, i);
  }
  
  gsl_permutation_free(vector_sorting);
}

void MultiPermutation::write_upper_bound_of_permutation_values_to_vector(gsl_vector_int* output){
  if(output->size < required_length_of_access_vector()) {
    std::cout <<"Error in write_upper_bound_of_permutation_values_to_vector: Length of output vector (";
    std::cout <<output->size<<") not large enough ("<<required_length_of_access_vector()<<")."<<std::endl;
    exit(1);
  }
  
  set_temp_vector_to_upper_bound_of_permutation_values();
  for(int i=0; i<temp_access_vector->size; i++) {
    // std::cout <<"DEBUG: Setting output vector element to: "<<gsl_vector_int_get(temp_access_vector,i)<<std::endl;
    gsl_vector_int_set( output, i, gsl_vector_int_get(temp_access_vector, i) );
  }
}

void MultiPermutation::set_reduced_temp_vector_to_reduced_upper_bound_of_permutation_values() {
  int element_length;
  int c = 0;
  for(int i=0; i<permutation_elements->size; i++) {
    element_length = gsl_vector_int_get(permutation_elements,i);
    // if( element_length > 1 ) {
      // reduced by one, because the last element of the permutation is determined by the others
      for(int j=0; j<element_length - 1; j++) {
        gsl_vector_int_set(temp_access_vector_reduced, c++, element_length - j);
      }
    // }
  }
}

void MultiPermutation::set_reduced_temp_vector_to_reduced_access_vector(const gsl_vector_int* access) {
  int element_length;
  int c = 0; // index of access vector 
  int r = 0; // index of reduced vector
  int upper_bound, input_value;
  int nr_of_lower_indices_used_previously;
  for(int i=0; i<permutation_elements->size; i++) {
    element_length = gsl_vector_int_get(permutation_elements,i);
    upper_bound = element_length;

    // in temp_access_vector we record which permutation elements have been used already
    gsl_vector_int_set_zero(temp_access_vector);
    for(int j=0; j<element_length; j++) {
      // unless we are at the last element, which we always ignore for the reduced vector
      if(j < element_length - 1) {
        input_value = gsl_vector_int_get(access, c);

        nr_of_lower_indices_used_previously = 0;
        for(int n=0; n<input_value; n++) {
          if( gsl_vector_int_get(temp_access_vector, n) != 0 ) { // index has been used
            nr_of_lower_indices_used_previously++;
          }
        }
        gsl_vector_int_set( temp_access_vector_reduced, r, input_value - nr_of_lower_indices_used_previously );
        gsl_vector_int_set( temp_access_vector, input_value, 1 );
        r++;
      }
      c++;
      upper_bound--;
    }
  }
}

void MultiPermutation::print_debug_info() {
  std::cout <<" ------ DEBUG info: MultiPermutation ------"<<std::endl;
  
  std::cout <<"required_length_of_access_vector = "<<required_length_of_access_vector()<<std::endl;
  std::cout <<"required_length_of_reduced_access_vector = "<<required_length_of_reduced_access_vector()<<std::endl;
  std::cout <<"memory use: "<<(mem->memory_usage_in_bytes())<<" Bytes"<<std::endl;
  
  std::cout <<"permutation_elements = ( ";
  for(int i=0; i<permutation_elements->size; i++) {
    std::cout <<gsl_vector_int_get(permutation_elements,i)<<" ";
  }
  std::cout <<")"<<std::endl;

  std::cout <<"temp_access_vector = ( ";
  for(int i=0; i<temp_access_vector->size; i++) {
    std::cout <<gsl_vector_int_get(temp_access_vector,i)<<" ";
  }
  std::cout <<")"<<std::endl;

  std::cout <<"temp_access_vector_reduced = ( ";
  for(int i=0; i<temp_access_vector_reduced->size; i++) {
    std::cout <<gsl_vector_int_get(temp_access_vector_reduced,i)<<" ";
  }
  std::cout <<")"<<std::endl;
  
  std::cout <<"debug info for mem:"<<std::endl;
  mem->print_debug_info();
}
