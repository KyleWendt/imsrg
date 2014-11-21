#include "ModelSpace.hh"
#include "ReadWrite.hh"
#include "Operator.hh"
#include "HartreeFock.hh"
#include "IMSRGSolver.hh"
#include "imsrg_util.hh"
#include <iostream>
#include <fstream>
#include <vector>
#include <stdlib.h>

#include "AngMom.hh"

using namespace std;

int main(int argc, char**argv)
{

   ReadWrite rw = ReadWrite();
   // If a settings file isn't given as a command line arg, look for the default file
   string settings_file = argc>1 ? string(argv[1]) : "settings.inp";
   rw.ReadSettingsFile(settings_file);

   // These are parameters that may be used in the settings file
   string inputsps	= rw.InputParameters["inputsps"];
   string inputtbme	= rw.InputParameters["inputtbme"];
   string darmstadttbme	= rw.InputParameters["darmstadttbme"];
   string darmstadtEmax	= rw.InputParameters["darmstadtEmax"];
   string jasontbme	= rw.InputParameters["jasontbme"];
   string flowfile	= rw.InputParameters["flowfile"];
   string ds_str	= rw.InputParameters["ds"];
   string smax_str	= rw.InputParameters["smax"];
   string generator	= rw.InputParameters["generator"];
   string comcorr	= rw.InputParameters["com-correction"];
   string bch_prod_thr	= rw.InputParameters["BCH-product-threshold"];
   string bch_trans_thr	= rw.InputParameters["BCH-transform-threshold"];
   string occfile	= rw.InputParameters["occupation-file"];
   string densfile	= rw.InputParameters["density-file"];

   if (generator == "") generator = "white";
   if (comcorr == "false" or comcorr == "False" or comcorr == "FALSE") rw.SetCoMCorr(false);


   cout << "Reading in the modelspace from " << inputsps << endl;
   ModelSpace modelspace = rw.ReadModelSpace(inputsps);
/*
   cout << "Calculating Mosh(0,0,1,0,0,1,0,1,0)..." << endl;
   double mosh = AngMom::Moshinsky(0,0,1,0,0,1,0,1,0);
   cout << "result: " << mosh << endl;

   Operator Top = imsrg_util::PSquaredOp(modelspace);
   
   cout << "I think <00|Top|00> = " << Top.TwoBody[0](0,0) << endl;

   rw.WriteTwoBody(Top,"../output/T.int");
   return 0;
*/
   Operator H_bare =  Operator(&modelspace);
   H_bare.SetHermitian(); // just to be sure

   Operator H_3N =  Operator(&modelspace);
   H_3N.SetHermitian(); // just to be sure

   rw.ReadBareTBME_Jason(jasontbme, H_3N);
   Operator H3NO = H_3N.DoNormalOrdering();
   H3NO.ZeroBody /= 3.0;
   H3NO.OneBody /= 2.0;

   cout << "Zero body part = " << H3NO.ZeroBody << endl;
   rw.WriteOneBody(H3NO,"../output/H3_1b_NO.out");
   rw.WriteTwoBody(H3NO,"../output/H3_2b_NO.out");

   H_bare.CalculateKineticEnergy();

   if (inputtbme != "")
   {
      cout << "Reading Oslo-style TBME from " << inputtbme << endl;
      rw.ReadBareTBME(inputtbme, H_bare);
      rw.WriteTwoBody(H_bare,"../output/Oslo_H_bare.out");
   }
   else if (darmstadttbme != "")
   {
      int Emax = darmstadtEmax != "" ? atoi(darmstadtEmax.c_str()) : -1;
      cout << "Reading Darmstadt-style TBME from " << darmstadttbme << " with Emax " << Emax << endl;
      rw.ReadBareTBME_Darmstadt(darmstadttbme, H_bare, Emax);
      rw.WriteTwoBody(H_bare,"../output/Darmstadt_H_bare.out");
   }

//   H_bare += H3NO;

   cout << "Norm of H_bare = " << H_bare.Norm() << endl;

   HartreeFock  hf = HartreeFock(H_bare);
   hf.Solve();

   Operator H_hf = hf.TransformToHFBasis(H_bare);
   HartreeFock hf2 = HartreeFock(H_hf);
   hf2.Solve();


   cout << "EHF = " << hf.EHF << endl;
   cout << "EHF2 = " << hf2.EHF << endl;
   Operator HFNO = H_hf.DoNormalOrdering();
   Operator HbareNO = H_bare.DoNormalOrdering();


   Operator H3N_hf = hf.TransformToHFBasis(H3NO);
   HFNO += H3N_hf;
   cout << "Norm of HFNO = " << HFNO.Norm() << endl;

   IMSRGSolver imsrgsolver = IMSRGSolver(HFNO);
//   IMSRGSolver imsrgsolver = IMSRGSolver(HbareNO);
   imsrgsolver.SetFlowFile(flowfile);
   imsrgsolver.SetGenerator(generator);
   if (bch_prod_thr != "")
   {
      double thr = strtod(bch_prod_thr.c_str(),NULL);
      Operator::Set_BCH_Product_Threshold(thr);
   }
   if (bch_trans_thr != "")
   {
      double thr = strtod(bch_trans_thr.c_str(),NULL);
      Operator::Set_BCH_Transform_Threshold(thr);
   }

   if (ds_str != "")
   {
      double ds = strtod(ds_str.c_str(),NULL);
      imsrgsolver.SetDs(ds);
      imsrgsolver.SetdOmega(ds);
   }
   if (smax_str != "")
   {
      double smax = strtod(smax_str.c_str(),NULL);
      imsrgsolver.SetSmax(smax);
   }

/////// THIS IS THE TIME CONSUMING PART //////////
   imsrgsolver.Solve();
//////////////////////////////////////////////////



  if (occfile != "" or densfile != "")
  {

    int nr_steps = 100;
    vector<double> R(nr_steps,0);
    double dr = 0.1;
    for (int i=0;i<nr_steps;++i) R[i] = i*dr;
  
    cout << "Calculating occupation numbers..." << endl;
  
    vector<double> occ = imsrg_util::GetOccupations(hf,imsrgsolver);
    //vector<double> occ = imsrg_util::GetOccupations(hf);

    // This should probably go in the ReadWrite class...
    if (occfile != "")
    {
      ofstream occf;
      occf.open(occfile,ofstream::out);
      for (int i=0;i<modelspace.GetNumberOrbits();i+=2)
      {
         Orbit *oi = modelspace.GetOrbit(i);
         cout << i << "  " << oi->n << " " << oi->l << "  " << oi->j2/2 << " " << occ[i] << " " << occ[i+1] << endl;
         occf << i << "  " << oi->n << " " << oi->l << "  " << oi->j2/2 << " " << occ[i] << " " << occ[i+1] << endl;
      }
      occf.close();
    }

    if (densfile  != "")
    {
      vector<double>dens_P = imsrg_util::GetDensity(occ,R,modelspace.proton_orbits,modelspace);
      vector<double>dens_N = imsrg_util::GetDensity(occ,R,modelspace.neutron_orbits,modelspace);

      ofstream densf;
      densf.open(densfile,ofstream::out);
      double rms_n = 0;
      double rms_p = 0;
      double norm_n = 0;
      double norm_p = 0;
      for (int i=0;i<R.size();++i)
      {
         cout << R[i] << " " << dens_P[i] << " " << dens_N[i] << endl;
         densf << R[i] << " " << dens_P[i] << " " << dens_N[i] << endl;
         rms_n += dens_N[i]*dens_N[i]*pow(R[i],4)*dr;
         rms_p += dens_P[i]*dens_P[i]*pow(R[i],4)*dr;
         norm_n += dens_N[i]*dens_N[i]*pow(R[i],2)*dr;
         norm_p += dens_P[i]*dens_P[i]*pow(R[i],2)*dr;
      }
      rms_n = sqrt(rms_n / norm_n );
      rms_p = sqrt(rms_p / norm_p );
      cout << "neutron rms: " << rms_n << endl;
      cout << "proton rms: "  << rms_p << endl;
      densf.close();
    }
  }
//  Operator Np0s1 = StandardOperators::NumberOp(modelspace,0,0,1,-1); // proton 0s1/2
//  Operator Np0s1_hf = hf.TransformToHFBasis(Np0s1);
//  Operator Np0s1_NO = Np0s1_hf.DoNormalOrdering();
//  Operator Np0s1_final = imsrgsolver.Transform(Np0s1_NO);
//  cout << "proton 0s1/2 occupation = " << Np0s1_final.ZeroBody << endl;

//   rw.WriteValenceOneBody(HFNO,"../output/O16_lmax6_SM_1b_bare.int");
//   rw.WriteValenceTwoBody(HFNO,"../output/O16_lmax6_SM_2b_bare.int");
//   rw.WriteValenceOneBody(imsrgsolver.H_s,"../output/O16_lmax6_SM_1b.int");
//   rw.WriteValenceTwoBody(imsrgsolver.H_s,"../output/O16_lmax6_SM_2b.int");

//   rw.WriteNuShellX_int(imsrgsolver.H_s,"../output/Ca40srg.int");
//   rw.WriteNuShellX_sps(imsrgsolver.H_s,"../output/Ca40srg.sp");

//   rw.WriteNuShellX_int(HFNO,"../output/He4_bare.int");

  return 0;
}