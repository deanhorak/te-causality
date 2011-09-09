// calculate the transfer entropy between a number of time series
// this is the extension to arbitrary Markov order of the source and target term
// created by olav, Wed 7 Sep 2011

#include <cstdlib>
#include <iostream>
#include <cassert>
#include <fstream>
#include <ctime>
#include <cstring>
#include <sstream>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_statistics_double.h>
#include <gsl/gsl_vector.h>

#include "../../Simulationen/olav.h"
#include "../../../Sonstiges/SimKernel/sim_main.h"
#include "../te-datainit.h"
#include "../multidimarray.h"

// #ifndef INLINE
// #define INLINE extern inline
// #endif

#define REPORTS 25
// #define SHOW_DETAILED_PROGRESS

#define OUTPUTNUMBER_PRECISION 15
#define SEPARATED_OUTPUT

// #define GSL_RANDOM_NUMBER_GENERATOR gsl_rng_default
#define GSL_RANDOM_NUMBER_GENERATOR gsl_rng_ranlxs2

#define COUNTARRAY_IPAST_GPAST 1
#define COUNTARRAY_INOW_IPAST_GPAST 2
#define COUNTARRAY_IPAST_JPAST_GPAST 3
#define COUNTARRAY_INOW_IPAST_JPAST_GPAST 4

using namespace std;

typedef unsigned char rawdata;

time_t start, middle, end, now;

class Kernel;

int main(int argc, char* argv[])
{
  SimControl<Kernel> simc;
  time(&start);
  int ret = simc.simulate(argc, argv);
  return 0;
};

class Kernel
{
public:
  unsigned long iteration;
  unsigned int size;
  unsigned int bins, globalbins;
  // unsigned long mag der SimKernel irgendwie nicht?
  long samples;
  long StartSampleIndex, EndSampleIndex;
  bool EqualSampleNumberQ;
  long MaxSampleNumberPerBin;
  unsigned long * AvailableSamples;
  double std_noise;
  string inputfile_name;
  string outputfile_results_name;
  string outputfile_pars_name;
  string spikeindexfile_name, spiketimesfile_name;
  string FluorescenceModel;

  double input_scaling;
  double cutoff;
  double tauF;
  double tauCa;
  double fluorescence_saturation;
  double DeltaCalciumOnAP;

  // parameters for light scattering
  std::string YAMLfilename;
  double SigmaScatter;
  double AmplitudeScatter;
  
  bool OverrideRescalingQ; // if, for example the input are pure spike data (integers)
  bool HighPassFilterQ; // actually, this transforms the signal into the difference signal
  bool InstantFeedbackTermQ;

  bool IncludeGlobalSignalQ;
  bool GenerateGlobalFromFilteredDataQ;
  double GlobalConditioningLevel;
  int SourceMarkovOrder, TargetMarkovOrder;
  
  bool ContinueOnErrorQ;
  bool skip_the_rest;
  
  bool AutoBinNumberQ;
  bool AutoConditioningLevelQ;

  gsl_rng* GSLrandom;

  unsigned long Tspace, Sspace;
  MultiDimArrayLong* F_Ipast_Gpast;
  MultiDimArrayLong* F_Inow_Ipast_Gpast;
  MultiDimArrayLong* F_Ipast_Jpast_Gpast;
  MultiDimArrayLong* F_Inow_Ipast_Jpast_Gpast;

  gsl_vector_int * vec_Full;
  gsl_vector_int * vec_Full_Bins;
  
  gsl_vector_int_view vec_Inow;
  gsl_vector_int_view vec_Ipast;
  gsl_vector_int_view vec_Jpast;
  // here the conditioning signal is fixed to order 1
  gsl_vector_int_view vec_Gpast;
  // int vec_G;
  gsl_vector_int* gsl_access;
  
  rawdata **xdata;
  rawdata *xglobal;
#ifndef SEPARATED_OUTPUT
  double **xresult;
  long double Hxx, Hxxy;
#else
  double ***xresult;
  long double *Hxx, *Hxxy;
#endif
  // double *xtest;
  // rawdata *xtestD;

  void initialize(Sim& sim)
  {
    iteration = sim.iteration();
    sim.io <<"Init: iteration "<<iteration<<", process "<< sim.process()<<Endl;
    time(&now);
    sim.io <<"time: ";
    sim.io <<"elapsed "<<sec2string(difftime(now,start));
    sim.io <<", ETA "<<ETAstring(sim.iteration()-1,sim.n_iterations(),difftime(now,start))<<Endl;
    
    // read parameters from control file
    sim.get("size",size);
    bins = 0;
    sim.get("AutoBinNumberQ",AutoBinNumberQ,false);
    if(!AutoBinNumberQ) sim.get("bins",bins);
    sim.get("globalbins",globalbins,1);
    if(globalbins<1) globalbins=1;
    sim.get("samples",samples);
    sim.get("StartSampleIndex",StartSampleIndex,1);
    sim.get("EndSampleIndex",EndSampleIndex,samples-1);
    sim.get("EqualSampleNumberQ",EqualSampleNumberQ,false);
    sim.get("MaxSampleNumberPerBin",MaxSampleNumberPerBin,-1);

    sim.get("noise",std_noise,-1.);
    sim.get("appliedscaling",input_scaling,1.);
    sim.get("cutoff",cutoff,-1.);
    sim.get("tauF",tauF);
    sim.get("OverrideRescalingQ",OverrideRescalingQ,false);
    sim.get("HighPassFilterQ",HighPassFilterQ,false);
    sim.get("InstantFeedbackTermQ",InstantFeedbackTermQ,false);

    sim.get("saturation",fluorescence_saturation,-1.);
    sim.get("IncludeGlobalSignalQ",IncludeGlobalSignalQ,false);
    assert(IncludeGlobalSignalQ ^ globalbins==1);
    sim.get("GenerateGlobalFromFilteredDataQ",GenerateGlobalFromFilteredDataQ,false);
    sim.get("AutoConditioningLevelQ",AutoConditioningLevelQ,false);
    if(!AutoConditioningLevelQ)
    {
      sim.get("GlobalConditioningLevel",GlobalConditioningLevel,-1.);
      if (GlobalConditioningLevel>0) assert(globalbins==2); // for now, to keep it simple
    }
    else GlobalConditioningLevel = -1.;
    
    sim.get("SourceMarkovOrder",SourceMarkovOrder,1);
    assert(SourceMarkovOrder>0);
    sim.get("TargetMarkovOrder",TargetMarkovOrder,1);
    assert(TargetMarkovOrder>0);
    
    sim.get("inputfile",inputfile_name,"");
    sim.get("outputfile",outputfile_results_name);
    sim.get("outputparsfile",outputfile_pars_name);
    sim.get("spikeindexfile",spikeindexfile_name,"");
    sim.get("spiketimesfile",spiketimesfile_name,"");
    // make sure we either have a fluorescence input or both spike inputs
    if(!((inputfile_name!="") ^ ((spikeindexfile_name!="")&&(spiketimesfile_name!=""))))
    {
      sim.io <<"Error: Based on the parameters, it is not clear where your data should come from."<<Endl;
      exit(1);
    }
    sim.get("FluorescenceModel",FluorescenceModel,"");
    sim.get("DeltaCalciumOnAP",DeltaCalciumOnAP,50);
    sim.get("tauCa",tauCa,1000);

    // parameters for light scattering
    sim.get("YAMLfile",YAMLfilename,"");
    sim.get("SigmaScatter",SigmaScatter,-1.);
    sim.get("AmplitudeScatter",AmplitudeScatter,-1.);
  
    sim.get("ContinueOnErrorQ",ContinueOnErrorQ,false);

    // initialize random number generator
    gsl_rng_env_setup();
    GSLrandom = gsl_rng_alloc(GSL_RANDOM_NUMBER_GENERATOR);
    gsl_rng_set(GSLrandom, 1234);
    
    AvailableSamples = NULL;
    xdata = NULL;
    xglobal = NULL;
    xresult = NULL;
  };

  void execute(Sim& sim)
  {
    sim.io <<"------ transferentropy-sim:extended-multidim ------ olav, Wed 7 Sep 2011 ------"<<Endl;
    // time_t start, middle, end;

    sim.io <<"output file: "<<outputfile_results_name<<Endl;
    // Gespeichert wird später - hier nur Test, ob das Zielverzeichnis existiert
    write_parameters();
    skip_the_rest = false;

    sim.io <<"allocating memory..."<<Endl;
    try {
#ifndef SEPARATED_OUTPUT
      xresult = new double*[size];
#else
      xresult = new double**[size];
#endif
      for(int i=0; i<size; i++)
      {
        // xdata[i] = new rawdata[samples];
        // memset(xdata[i], 0, samples*sizeof(rawdata));
#ifndef SEPARATED_OUTPUT
        xresult[i] = new double[size];
        memset(xresult[i], 0, size*sizeof(double));
#else
        xresult[i] = new double*[size];
        for(int i2=0; i2<size; i2++)
        {
          xresult[i][i2] = new double[globalbins];
          memset(xresult[i][i2], 0, globalbins*sizeof(double));
        }
#endif
      }
    
#ifdef SEPARATED_OUTPUT
      // for testing
      Hxx = new long double[globalbins];
      Hxxy = new long double[globalbins];
#endif
      AvailableSamples = new unsigned long[globalbins];
      
      // hack of medium ugliness to make it work without global signal
      if(globalbins<=1)
      {
        // sim.io <<"debug: xglobal hack."<<Endl;
        xglobal = new rawdata[samples];
        memset(xglobal, 0, samples*sizeof(rawdata));
        AvailableSamples[0] = EndSampleIndex-StartSampleIndex+1;
      }
      sim.io <<" -> done."<<Endl;     
      
      double** xdatadouble = NULL;
      if(inputfile_name!="")
        sim.io <<"input file: \""<<inputfile_name<<"\""<<Endl;
      else {
        sim.io <<"input files:"<<Endl;
        sim.io <<"- spike indices: \""<<spikeindexfile_name<<"\""<<Endl;
        sim.io <<"- spike times: \""<<spiketimesfile_name<<"\""<<Endl;
      }
      
      if(inputfile_name=="") {
        sim.io <<"loading data and generating time series from spike data..."<<Endl;
        xdatadouble = generate_time_series_from_spike_data(spiketimesfile_name, spikeindexfile_name, size, int(round(tauF)), samples, FluorescenceModel, std_noise, fluorescence_saturation, cutoff, DeltaCalciumOnAP, tauCa, GSLrandom, sim);
      }
      else {
        sim.io <<"loading data from binary file..."<<Endl;
        xdatadouble = load_time_series_from_binary_file(inputfile_name, size, samples, input_scaling, OverrideRescalingQ, std_noise, fluorescence_saturation, cutoff, GSLrandom, sim);
      }
      sim.io <<" -> done."<<Endl;
      
      if(AmplitudeScatter>0.) {
        sim.io <<"simulating light scattering..."<<Endl;
        apply_light_scattering_to_time_series(xdatadouble, size, samples, YAMLfilename, SigmaScatter, AmplitudeScatter, sim);
        sim.io <<" -> done."<<Endl;
      }

      // sim.io <<"histogram of averaged signal:"<<Endl;
      // double* xmean = generate_mean_time_series(xdatadouble,size,samples);
      // PlotLogHistogramInASCII(xmean,samples,smallest(xmean,samples),largest(xmean,samples),"<fluoro>","#",sim);
      // free_time_series_memory(xmean);
      // cout <<"DEBUG: subset of first node: ";
      // display_subset(xdatadouble[0]);
      if(AutoConditioningLevelQ) {
        sim.io <<"guessing optimal conditioning level..."<<Endl;
        GlobalConditioningLevel = Magic_GuessConditioningLevel(xdatadouble,size,samples,sim);
        sim.io <<" -> conditioning level is: "<<GlobalConditioningLevel<<Endl;
        sim.io <<" -> done."<<Endl;
      }      
      
      if((globalbins>1)&&(!GenerateGlobalFromFilteredDataQ)) {
        sim.io <<"generating discretized global signal..."<<Endl;
        xglobal = generate_discretized_global_time_series(xdatadouble, size, samples, globalbins, GlobalConditioningLevel, AvailableSamples, StartSampleIndex, EndSampleIndex, sim);
        sim.io <<" -> done."<<Endl;
      }
      
      if(HighPassFilterQ) {
        sim.io <<"applying high-pass filter to time series..."<<Endl;
        apply_high_pass_filter_to_time_series(xdatadouble, size, samples);
        sim.io <<" -> done."<<Endl;
      }

      if(AutoBinNumberQ) {
        sim.io <<"guessing optimal bin number..."<<Endl;
        bins = Magic_GuessBinNumber(xdatadouble,size,samples);
        sim.io <<" -> number of bins is: "<<bins<<Endl;
        sim.io <<" -> done."<<Endl;
      }
      
      // Now we know the number of local bins to use, so we can reserve the discretized memory:
      // This is overall iterator that will be mapped onto array indices later:
      vec_Full = NULL;
      vec_Full_Bins = NULL;
      vec_Full = gsl_vector_int_alloc(1+TargetMarkovOrder+SourceMarkovOrder+1);
      gsl_vector_int_set_zero(vec_Full);
      vec_Full_Bins = gsl_vector_int_alloc(1+TargetMarkovOrder+SourceMarkovOrder+1);
      gsl_vector_int_set_all(vec_Full_Bins,bins);
      gsl_vector_int_set(vec_Full_Bins,1+TargetMarkovOrder+SourceMarkovOrder,globalbins);
      gsl_access = gsl_vector_int_alloc(1+TargetMarkovOrder+SourceMarkovOrder+1);
    
      // Initialize views to have better access to the full iterator elements:
      vec_Inow = gsl_vector_int_subvector(vec_Full,0,1);
      vec_Ipast = gsl_vector_int_subvector(vec_Full,1,TargetMarkovOrder);
      vec_Jpast = gsl_vector_int_subvector(vec_Full,1+TargetMarkovOrder,SourceMarkovOrder);
      vec_Gpast = gsl_vector_int_subvector(vec_Full,1+TargetMarkovOrder+SourceMarkovOrder,1);
      
      // here we assume equal binning for source and target terms
      // ------------------ IndexMultipliers_Ipast_Gpast:
      gsl_vector_int* BinsPerDim = gsl_vector_int_alloc(TargetMarkovOrder+1);
      for (int i=0; i<TargetMarkovOrder; i++)
        gsl_vector_int_set(BinsPerDim,i,bins);
      gsl_vector_int_set(BinsPerDim,TargetMarkovOrder,globalbins);
      F_Ipast_Gpast = new MultiDimArrayLong(BinsPerDim);
      gsl_vector_int_free(BinsPerDim);
    
      // ------------------ IndexMultipliers_Inow_Ipast_Gpast:
      BinsPerDim = gsl_vector_int_alloc(1+TargetMarkovOrder+1);
      for (int i=0; i<1+TargetMarkovOrder; i++)
        gsl_vector_int_set(BinsPerDim,i,bins);
      gsl_vector_int_set(BinsPerDim,TargetMarkovOrder+1,globalbins);
      F_Inow_Ipast_Gpast = new MultiDimArrayLong(BinsPerDim);
      gsl_vector_int_free(BinsPerDim);
    
      // ------------------ IndexMultipliers_Ipast_Jpast_Gpast:
      BinsPerDim = gsl_vector_int_alloc(TargetMarkovOrder+SourceMarkovOrder+1);
      for (int i=0; i<TargetMarkovOrder+SourceMarkovOrder; i++)
        gsl_vector_int_set(BinsPerDim,i,bins);
      gsl_vector_int_set(BinsPerDim,TargetMarkovOrder+SourceMarkovOrder,globalbins);
      F_Ipast_Jpast_Gpast = new MultiDimArrayLong(BinsPerDim);
      gsl_vector_int_free(BinsPerDim);
    
      // ------------------ IndexMultipliers_Inow_Ipast_Jpast_Gpast:
      BinsPerDim = gsl_vector_int_alloc(1+TargetMarkovOrder+SourceMarkovOrder+1);
      for (int i=0; i<1+TargetMarkovOrder+SourceMarkovOrder; i++)
        gsl_vector_int_set(BinsPerDim,i,bins);
      gsl_vector_int_set(BinsPerDim,1+TargetMarkovOrder+SourceMarkovOrder,globalbins);
      F_Inow_Ipast_Jpast_Gpast = new MultiDimArrayLong(BinsPerDim);
      gsl_vector_int_free(BinsPerDim);
    
           
      if((globalbins>1)&&(GenerateGlobalFromFilteredDataQ)) {
        sim.io <<"generating discretized global signal..."<<Endl;
        xglobal = generate_discretized_global_time_series(xdatadouble, size, samples, globalbins, GlobalConditioningLevel, AvailableSamples, StartSampleIndex, EndSampleIndex, sim);
        sim.io <<" -> done."<<Endl;
      }

      sim.io <<"discretizing local time series..."<<Endl;
      xdata = generate_discretized_version_of_time_series(xdatadouble, size, samples, bins);
      // and, since double version of time series is not used any more...
      if(xdatadouble!=NULL) free_time_series_memory(xdatadouble, size);
      sim.io <<" -> done."<<Endl;

      // cout <<"DEBUG: subset of discretized global signal: ";
      // display_subset(xglobal);
    }
    catch(...) {
      sim.io <<"Error: could not reserve enough memory!"<<Endl;
      if(!ContinueOnErrorQ) exit(1);
      else
      {
        sim.io <<"Error handling: ContinueOnErrorQ flag set, proceeding..."<<Endl;
        skip_the_rest = true;
      }
    }
    // sim.io <<" -> done."<<Endl;
  
    // cout <<"testing discretization:"<<endl;
    // xtest = new double[100];
    // xtestD = new rawdata[100];
    // for(int i=0;i<100;i++)
    //  xtest[i] = i;
    // discretize(xtest,xtestD,smallest(xtest,100),largest(xtest,100),100,bins);
    // for(int i=0;i<100;i++)
    //  cout <<i<<": "<<xtest[i]<<" -> "<<(int)xtestD[i]<<endl;
    // exit(0);
    
    // cout <<"testing GSL vector class:"<<endl;
    // bool runningI = true;
    // while(runningI)
    // {
    //  SimplePrintGSLVector(vec_Full);
    //  runningI = OneStepAhead_FullIterator();
    // }
    
    if (!skip_the_rest) {
    // SetUpMultidimArrayIndexMultipliers();
    
    
  
    // main loop:
    sim.io <<"set-up: "<<size<<" nodes, ";
    sim.io <<EndSampleIndex-StartSampleIndex+1<<" out of "<<samples<<" samples, ";
    sim.io <<bins<<" bins, "<<globalbins<<" globalbins"<<Endl;
    sim.io <<"set-up: Markov order of source/target/conditioning: "<<SourceMarkovOrder<<"/"<<TargetMarkovOrder<<"/1"<<Endl;
#ifdef SEPARATED_OUTPUT
    sim.io <<"set-up: separated output (globalbin)"<<Endl;
#endif
  
    time(&start);
    sim.io <<"start: "<<ctime(&start)<<Endl;
#ifdef SHOW_DETAILED_PROGRESS
    sim.io <<"running ";
#else
    sim.io <<"running..."<<Endl;
    bool status_already_displayed = false;
#endif

    for(int ii=0; ii<size; ii++)
    {
#ifdef SHOW_DETAILED_PROGRESS
      status(ii,REPORTS,size);
#else
      time(&middle);
      if ((!status_already_displayed)&&((ii>=size/3)||((middle-start>30.)&&(ii>0))))
      { 
        sim.io <<" (after "<<ii<<" nodes: elapsed "<<sec2string(difftime(middle,start)) \
          <<", ETA "<<ETAstring(ii,size,difftime(middle,start))<<")"<<Endl;
        status_already_displayed = true;
      }
#endif
      for(int jj=0; jj<size; jj++)
      {
        if (ii != jj)
        {
#ifndef SEPARATED_OUTPUT
          xresult[jj][ii] = TransferEntropy(xdata[ii], xdata[jj]);
#else
          TransferEntropySeparated(xdata[ii], xdata[jj], ii, jj);
#endif
        }
        // else xresult[ii][jj] = 0.0;
      }
    }
#ifndef SHOW_DETAILED_PROGRESS
    sim.io <<" -> done."<<Endl;
#endif

    time(&end);
    sim.io <<"end: "<<ctime(&end)<<Endl;
    sim.io <<"runtime: "<<sec2string(difftime(end,start))<<Endl;

    // cout <<"TE terms: "<<terms_sum<<", of those zero: "<<terms_zero<<" ("<<int(double(terms_zero)*100/terms_sum)<<"%)"<<endl;
    
  }
  };
  
  void finalize(Sim& sim)
  {
    if(!skip_the_rest) {
#ifdef SEPARATED_OUTPUT
      write_multidim_result(xresult,globalbins);
#else
      write_result(xresult);
#endif
      write_parameters();

      // free allocated memory
      gsl_rng_free(GSLrandom);
      gsl_vector_int_free(vec_Full);
      gsl_vector_int_free(vec_Full_Bins);
    }

    try {
#ifdef SEPARATED_OUTPUT
    for (int x=0; x<size; x++)
    {
      for (int x2=0; x2<size; x2++)
        delete[] xresult[x][x2];
      delete[] xresult[x];
    }
    delete[] Hxx;
    delete[] Hxxy;
#endif
    delete[] xresult;
    
    if (AvailableSamples != NULL) delete[] AvailableSamples;

    delete F_Ipast_Gpast;
    delete F_Inow_Ipast_Gpast;
    delete F_Ipast_Jpast_Gpast;
    delete F_Inow_Ipast_Jpast_Gpast;

    if(xdata != NULL) free_time_series_memory(xdata,size);
    if(xglobal != NULL) free_time_series_memory(xglobal);
    }
    catch(...) {};
    
    sim.io <<"End of Kernel (iteration="<<(sim.iteration())<<")"<<Endl;
  };
  

#ifdef SEPARATED_OUTPUT
  void TransferEntropySeparated(rawdata *arrayI, rawdata *arrayJ, int I, int J)
#else
  double TransferEntropy(rawdata *arrayI, rawdata *arrayJ)
#endif
  {
    // see for reference:
    //      Gourevitch und Eggermont. Evaluating Information Transfer Between Auditory
    //      Cortical Neurons. Journal of Neurophysiology (2007) vol. 97 (3) pp. 2533
    // We are looking at the information flow of array1 ("J") -> array2 ("I")
  
    // clear memory
#ifdef SEPARATED_OUTPUT
    memset(Hxx, 0, globalbins*sizeof(long double));
    memset(Hxxy, 0, globalbins*sizeof(long double));
#else
    double result = 0.0;
    Hxx = Hxxy = 0.0;
#endif
    F_Ipast_Gpast->clear();
    F_Inow_Ipast_Gpast->clear();
    F_Ipast_Jpast_Gpast->clear();
    F_Inow_Ipast_Jpast_Gpast->clear();
  
    // extract probabilities (actually number of occurrence)
    unsigned long const JShift = (unsigned long const)InstantFeedbackTermQ;
    assert(StartSampleIndex >= max(TargetMarkovOrder,SourceMarkovOrder));
    for (unsigned long t=StartSampleIndex; t<EndSampleIndex; t++)
    {
      // prepare the index vector vec_Full via the vector views
      gsl_vector_int_set(&vec_Inow.vector,0,arrayI[t]);
      // for (int i=0; i<TargetMarkovOrder; i++)
      //  gsl_vector_int_set(&vec_Ipast.vector,i,arrayI[t-1+JShift-i]);
      // for (int i=0; i<SourceMarkovOrder; i++)
      //  gsl_vector_int_set(&vec_Jpast.vector,i,arrayJ[t-1+JShift-i]);
      for (int i=0; i<TargetMarkovOrder; i++)
        gsl_vector_int_set(&vec_Ipast.vector,i,arrayI[t-1-i]);
        
      for (int i=0; i<SourceMarkovOrder; i++)
        gsl_vector_int_set(&vec_Jpast.vector,i,arrayJ[t-1+JShift-i]);
        
      gsl_vector_int_set(&vec_Gpast.vector,0,xglobal[t-1+JShift]);
      
      // int bla = gsl_vector_int_get(&vec_Gpast.vector,0);
      // assert(bla == 0);
      
      // add counts to arrays
      if (xglobal[t-1+JShift]<globalbins) { // for EqualSampleNumberQ flag
        set_up_access_vector(COUNTARRAY_IPAST_GPAST);
        F_Ipast_Gpast->inc(gsl_access);
        set_up_access_vector(COUNTARRAY_INOW_IPAST_GPAST);
        F_Inow_Ipast_Gpast->inc(gsl_access);
        set_up_access_vector(COUNTARRAY_IPAST_JPAST_GPAST);
        F_Ipast_Jpast_Gpast->inc(gsl_access);
        set_up_access_vector(COUNTARRAY_INOW_IPAST_JPAST_GPAST);
        F_Inow_Ipast_Jpast_Gpast->inc(gsl_access);
      }

      // DEBUG: test countings
      // if (t<50)
      // {
      //  cout <<"t = "<<t<<", F_Full: ";
      //  SimplePrintFullIterator(false);
      //  cout <<", F_Inow_Ipast_Jpast_Gpast = "<<F_Inow_Ipast_Jpast_Gpast[CounterArrayIndex(COUNTARRAY_INOW_IPAST_JPAST_GPAST)]<<endl;
      // 
      // }
        
    }

    // DEBUG: test countings
    // gsl_vector_int_set_zero(vec_Full);
    // bool runningIt2 = true;
    // while (runningIt2)
    // {
    //  SimplePrintFullIterator(false);
    //  cout <<", F_Inow_Ipast_Jpast_Gpast = "<<F_Inow_Ipast_Jpast_Gpast[CounterArrayIndex(COUNTARRAY_INOW_IPAST_JPAST_GPAST)]<<endl;
    //  
    //  runningIt2 = OneStepAhead_FullIterator();
    // }
    // exit(0);

    // Here is some space for elaborate debiasing... :-)
    
    // Calculate transfer entropy from plug-in estimator:
    gsl_vector_int_set_zero(vec_Full);
    bool runningIt = true;
    unsigned long ig, iig, ijg,iijg;
    long double igd, iigd, ijgd,iijgd;
    while (runningIt)
    {
      // SimplePrintGSLVector(vec_Full);
      set_up_access_vector(COUNTARRAY_IPAST_GPAST);
      ig = F_Ipast_Gpast->get(gsl_access);
      igd = (long double)ig;
      set_up_access_vector(COUNTARRAY_INOW_IPAST_GPAST);
      iig = F_Inow_Ipast_Gpast->get(gsl_access);
      if (iig!=0)
      {
        iigd = (long double)iig;
        set_up_access_vector(COUNTARRAY_IPAST_JPAST_GPAST);
        ijg = F_Ipast_Jpast_Gpast->get(gsl_access);
        ijgd = (long double)ijg;
        set_up_access_vector(COUNTARRAY_INOW_IPAST_JPAST_GPAST);
        iijg = F_Inow_Ipast_Jpast_Gpast->get(gsl_access);
        iijgd = (long double)iijg;
      
        // a.) calculate Hxx:
        if (gsl_vector_int_isnull(&vec_Jpast.vector)) // to avoid counting the same term multiple times
        {q  s
          if (iig!=0)
#ifdef SEPARATED_OUTPUT
            Hxx[gsl_vector_int_get(&vec_Gpast.vector,0)] -= iigd/AvailableSamples[gsl_vector_int_get(&vec_Gpast.vector,0)] * log(iigd/igd);
#else
            Hxx -= iigd/AvailableSamples[gsl_vector_int_get(&vec_Gpast.vector,0)] * log(iigd/igd);        
#endif
        }
      
        // b.) calculate Hxxy:
        if (iijg!=0)
#ifdef SEPARATED_OUTPUT
          Hxxy[gsl_vector_int_get(&vec_Gpast.vector,0)] -= iijgd/AvailableSamples[gsl_vector_int_get(&vec_Gpast.vector,0)] * log(iijgd/ijgd);       
#else
          Hxxy -= iijgd/AvailableSamples[gsl_vector_int_get(&vec_Gpast.vector,0)] * log(iijgd/ijgd);        
#endif
      }
      runningIt = OneStepAhead_FullIterator();
    }
    
#ifdef SEPARATED_OUTPUT
    for (rawdata g=0; g<globalbins; g++)
    {
      Hxx[g] /= log(2);
      Hxxy[g] /= log(2);
      xresult[J][I][g] = double(Hxx[g] - Hxxy[g]);
    }
#else
    return double((Hxx - Hxxy)/log(2));
#endif

    // DEBUG
    // cout <<endl;
    // for (char g=0; g<globalbins; g++)
    // {
    //  cout <<"Hxx="<<Hxx[g]<<", Hxxy="<<Hxxy[g]<<endl;
    //  cout <<"-> result for g="<<int(g)<<": "<<xresult[J][I][g]<<endl;
    // }
    // exit(0);
  };

  std::string bool2textMX(bool value)
  {
    if (value) return "True";
    else return "False";
  };

  void write_parameters()
  {
    char* name = new char[outputfile_pars_name.length()+1];
    strcpy(name,outputfile_pars_name.c_str());
    ofstream fileout1(name);
    delete[] name;
    if (fileout1 == NULL)
    {
      cerr <<endl<<"error: cannot open output file!"<<endl;
      exit(1);
    }

    fileout1.precision(6);
    fileout1 <<"{";
    fileout1 <<"executable->teextendedsim";
    fileout1 <<", iteration->"<<iteration;
    time(&end);
    fileout1 <<", ExecutionTime->"<<sec2string(difftime(end,start));
    
    fileout1 <<", size->"<<size;
    fileout1 <<", bins->"<<bins;
    fileout1 <<", globalbins->"<<globalbins;
    fileout1 <<", appliedscaling->"<<input_scaling;
    fileout1 <<", samples->"<<samples;
    fileout1 <<", StartSampleIndex->"<<StartSampleIndex;
    fileout1 <<", EndSampleIndex->"<<EndSampleIndex;
    fileout1 <<", EqualSampleNumberQ->"<<bool2textMX(EqualSampleNumberQ);
    fileout1 <<", MaxSampleNumberPerBin->"<<MaxSampleNumberPerBin;
    fileout1 <<", AvailableSamples->{";
    for (int i=0; i<globalbins; i++)
    {
      if (i>0) fileout1 <<",";
      if (AvailableSamples == NULL) fileout1 <<"?";
      else fileout1 <<AvailableSamples[i];
    }
    fileout1 <<"}";

    fileout1 <<", cutoff->"<<cutoff;
    fileout1 <<", noise->"<<std_noise;
    fileout1 <<", tauF->"<<tauF;
    fileout1 <<", tauCa->"<<tauCa;
    fileout1 <<", DeltaCalciumOnAP->"<<DeltaCalciumOnAP;
    fileout1 <<", OverrideRescalingQ->"<<bool2textMX(OverrideRescalingQ);
    fileout1 <<", HighPassFilterQ->"<<bool2textMX(HighPassFilterQ);
    fileout1 <<", InstantFeedbackTermQ->"<<bool2textMX(InstantFeedbackTermQ);

    fileout1 <<", ContinueOnErrorQ->"<<bool2textMX(ContinueOnErrorQ);
    fileout1 <<", saturation->"<<fluorescence_saturation;
    fileout1 <<", IncludeGlobalSignalQ->"<<bool2textMX(IncludeGlobalSignalQ);
    fileout1 <<", GenerateGlobalFromFilteredDataQ->"<<bool2textMX(GenerateGlobalFromFilteredDataQ);
    fileout1 <<", GlobalConditioningLevel->"<<GlobalConditioningLevel;
    fileout1 <<", TargetMarkovOrder->"<<TargetMarkovOrder;
    fileout1 <<", SourceMarkovOrder->"<<SourceMarkovOrder;
    
    fileout1 <<", AutoBinNumberQ->"<<bool2textMX(AutoBinNumberQ);
    fileout1 <<", AutoConditioningLevelQ->"<<bool2textMX(AutoConditioningLevelQ);
    
    
    fileout1 <<", inputfile->\""<<inputfile_name<<"\"";
    fileout1 <<", outputfile->\""<<outputfile_results_name<<"\"";
    fileout1 <<", spikeindexfile->\""<<spikeindexfile_name<<"\"";
    fileout1 <<", spiketimesfile->\""<<spiketimesfile_name<<"\"";
    fileout1 <<", FluorescenceModel->\""<<FluorescenceModel<<"\"";
    // parameters for light scattering
    fileout1 <<", YAMLfile->\""<<YAMLfilename<<"\"";
    fileout1 <<", SigmaScatter->"<<SigmaScatter;
    fileout1 <<", AmplitudeScatter->"<<AmplitudeScatter;
    fileout1 <<"}"<<endl;
    
    fileout1.close();
  };

  void write_result(double **array)
  {
    char* name = new char[outputfile_results_name.length()+1];
    strcpy(name,outputfile_results_name.c_str());
    ofstream fileout1(name);
    delete[] name;
    if (fileout1 == NULL)
    {
      cerr <<endl<<"error: cannot open output file!"<<endl;
      exit(1);
    }   

    fileout1.precision(OUTPUTNUMBER_PRECISION);
    fileout1 <<fixed;
    fileout1 <<"{";
    for(int j=0; j<size; j++)
    {
      if(j>0) fileout1<<",";
      fileout1 <<"{";
      for(unsigned long i=0; i<size; i++)
      {
        if (i>0) fileout1<<",";
        fileout1 <<(double)array[j][i];
      }
      fileout1 <<"}"<<endl;
    }
    fileout1 <<"}"<<endl;

    cout <<"Transfer entropy matrix saved."<<endl;
  };

  void write_multidim_result(double ***array, unsigned int dimens)
  {
    char* name = new char[outputfile_results_name.length()+1];
    strcpy(name,outputfile_results_name.c_str());
    ofstream fileout1(name);
    delete[] name;
    if (fileout1 == NULL)
    {
      cerr <<endl<<"error: cannot open output file!"<<endl;
      exit(1);
    }   

    fileout1.precision(OUTPUTNUMBER_PRECISION);
    fileout1 <<fixed;
    fileout1 <<"{";
    for(unsigned long j=0; j<size; j++)
    {
      if(j>0) fileout1<<",";
      fileout1 <<"{";
      for(unsigned long i=0; i<size; i++)
      {
        if (i>0) fileout1<<",";
        fileout1 <<"{";
        for(int k=0; k<dimens; k++)
        {
          if (k>0) fileout1<<",";
          fileout1 <<array[j][i][k];
        }
        fileout1 <<"}";
      }
      fileout1 <<"}"<<endl;
    }
    fileout1 <<"}"<<endl;

    cout <<"Transfer entropy matrix saved."<<endl;
  };

  bool OneStepAhead_FullIterator()
  {
    for(int i=0; i<=1+TargetMarkovOrder+SourceMarkovOrder+1; i++)
    {
      if (i==1+TargetMarkovOrder+SourceMarkovOrder+1) return false; // if we have reached the "maximum" value
      gsl_vector_int_set(vec_Full,i,gsl_vector_int_get(vec_Full,i)+1);
      
      if (gsl_vector_int_get(vec_Full,i) >= gsl_vector_int_get(vec_Full_Bins,i))
      {
        gsl_vector_int_set(vec_Full,i,0);
        if((i==1+TargetMarkovOrder+SourceMarkovOrder)&&(GlobalConditioningLevel>0.0))
          return false; // if we have reached the effective maximum, because we don't want to go through more
      }
      else break;
    }
    return true;
  };
  
  void SimplePrintGSLVector(gsl_vector_int* vec, Sim& sim)
  {
    SimplePrintGSLVector(vec,true,sim);
  };
  void SimplePrintGSLVector(gsl_vector_int* vec, bool newline, Sim& sim)
  {
    for(int i=0; i<vec->size; i++)
      sim.io <<gsl_vector_int_get(vec,i)<<" ";
    if (newline) sim.io <<Endl;
  };

  void SimplePrintFullIterator()
  {
    SimplePrintFullIterator(true);
  };
  void SimplePrintFullIterator(bool newline)
  {
    cout <<"Inow: "<<gsl_vector_int_get(&vec_Inow.vector,0);
    cout <<" Ipast: ";
    for (int i=0; i<TargetMarkovOrder; i++)
      cout <<gsl_vector_int_get(&vec_Ipast.vector,i)<<" ";
    cout <<"Jpast: ";
    for (int i=0; i<SourceMarkovOrder; i++)
      cout <<gsl_vector_int_get(&vec_Jpast.vector,i)<<" ";
    cout <<"Gpast: "<<gsl_vector_int_get(&vec_Gpast.vector,0);
    if (newline) cout <<endl;
  };
  
  // The following assumes that vec_Full has been set already (!)
  void set_up_access_vector(int arraycode)
  {
    switch (arraycode)
    {
      case COUNTARRAY_IPAST_GPAST:
        for (int i=0; i<TargetMarkovOrder; i++)
          gsl_vector_int_set(gsl_access,i,gsl_vector_int_get(&vec_Ipast.vector,i));
        gsl_vector_int_set(gsl_access,TargetMarkovOrder,gsl_vector_int_get(&vec_Gpast.vector,0));
        break;

      case COUNTARRAY_INOW_IPAST_GPAST:
        gsl_vector_int_set(gsl_access,0,gsl_vector_int_get(&vec_Inow.vector,0));
        for (int i=0; i<TargetMarkovOrder; i++)
          gsl_vector_int_set(gsl_access,1+i,gsl_vector_int_get(&vec_Ipast.vector,i));
        gsl_vector_int_set(gsl_access,1+TargetMarkovOrder,gsl_vector_int_get(&vec_Gpast.vector,0));
        break;

      case COUNTARRAY_IPAST_JPAST_GPAST:
        for (int i=0; i<TargetMarkovOrder; i++)
          gsl_vector_int_set(gsl_access,i,gsl_vector_int_get(&vec_Ipast.vector,i));
        for (int i=0; i<SourceMarkovOrder; i++)
          gsl_vector_int_set(gsl_access,TargetMarkovOrder+i,gsl_vector_int_get(&vec_Jpast.vector,i));
        gsl_vector_int_set(gsl_access,SourceMarkovOrder+TargetMarkovOrder,gsl_vector_int_get(&vec_Gpast.vector,0));
        break;

      case COUNTARRAY_INOW_IPAST_JPAST_GPAST:
        gsl_vector_int_set(gsl_access,0,gsl_vector_int_get(&vec_Inow.vector,0));
        for (int i=0; i<TargetMarkovOrder; i++)
          gsl_vector_int_set(gsl_access,1+i,gsl_vector_int_get(&vec_Ipast.vector,i));
        for (int i=0; i<SourceMarkovOrder; i++)
          gsl_vector_int_set(gsl_access,1+TargetMarkovOrder+i,gsl_vector_int_get(&vec_Jpast.vector,i));
        gsl_vector_int_set(gsl_access,1+TargetMarkovOrder+SourceMarkovOrder,gsl_vector_int_get(&vec_Gpast.vector,0));
        break;

      default:
        cout <<endl<<"GetCounterArray: error, invalid array code."<<endl; exit(1);
    }
  }
};