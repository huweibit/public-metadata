// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Author: Wei Hu
// =============================================================================

// General Includes
#include <cassert>
#include <cstdlib>
#include <ctime>

#include "chrono/physics/ChSystemSMC.h"
#include "chrono/utils/ChUtilsCreators.h"
#include "chrono/utils/ChUtilsGenerators.h"
#include "chrono/utils/ChUtilsGeometry.h"
#include "chrono/assets/ChBoxShape.h"

// Chrono fsi includes
#include "chrono_fsi/ChSystemFsi.h"
#include "chrono_fsi/utils/ChUtilsGeneratorFsi.h"
#include "chrono_fsi/utils/ChUtilsJSON.h"
#include "chrono_fsi/utils/ChUtilsPrintSph.cuh"

#define AddBoundaries

// Chrono namespaces
using namespace chrono;
using namespace collision;

using std::cout;
using std::endl;
std::ofstream simParams;

//----------------------------
// output directories and settings
//----------------------------
const std::string out_dir = GetChronoOutputPath() + "FSI_Sphere_Drop/";
std::string demo_dir;
bool pv_output = true;
typedef fsi::Real Real;

Real smalldis = 1.0e-9;
/// Dimension of the space domain
Real bxDim = 0.0 + smalldis;
Real byDim = 0.0 + smalldis;
Real bzDim = 0.0 + smalldis;
/// Dimension of the fluid domain
Real fxDim = 0.0 + smalldis;
Real fyDim = 0.0 + smalldis;
Real fzDim = 0.0 + smalldis;

double sphere_radius;
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
/// Forward declaration of helper functions
void SaveParaViewFiles(fsi::ChSystemFsi& myFsiSystem,
                       ChSystemSMC& mphysicalSystem,
                       std::shared_ptr<fsi::SimParams> paramsH,
                       int tStep,
                       double mTime);

void AddWall(std::shared_ptr<ChMaterialSurface> mat, std::shared_ptr<ChBody> body, const ChVector<>& dim, const ChVector<>& loc) {
    body->GetCollisionModel()->AddBox(mat, dim.x(), dim.y(), dim.z(), loc);
    auto box = chrono_types::make_shared<ChBoxShape>();
    box->GetBoxGeometry().Size = dim;
    box->GetBoxGeometry().Pos = loc;
}
//------------------------------------------------------------------
// Create the objects of the MBD system. Rigid bodies, and if fsi, their
// bce representation are created and added to the systems
//------------------------------------------------------------------

void CreateSolidPhase(ChSystemSMC& mphysicalSystem,
                      fsi::ChSystemFsi& myFsiSystem,
                      std::shared_ptr<fsi::SimParams> paramsH);
void ShowUsage() {
    cout << "usage: ./demo_FSI_Granular_SphereDrop <json_file>" << endl;
}

// =============================================================================
int main(int argc, char* argv[]) {
    // create a physics system
    ChSystemSMC mphysicalSystem;
    fsi::ChSystemFsi myFsiSystem(mphysicalSystem);
    // Get the pointer to the system parameter and use a JSON file to fill it out with the user parameters
    std::shared_ptr<fsi::SimParams> paramsH = myFsiSystem.GetSimParams();
    std::string inputJson = GetChronoDataFile("fsi/input_json/demo_FSI_SphereDrop_granular.json");
    if (argc == 1 && fsi::utils::ParseJSON(inputJson.c_str(), paramsH, fsi::mR3(bxDim, byDim, bzDim))) {
    } else if (argc == 2 && fsi::utils::ParseJSON(argv[1], paramsH, fsi::mR3(bxDim, byDim, bzDim))) {
    } else {
        ShowUsage();
        return 1;
    }

    /// Dimension of the space domain
    bxDim = paramsH->boxDimX + smalldis;
    byDim = paramsH->boxDimY + smalldis;
    bzDim = paramsH->boxDimZ + smalldis;
    /// Dimension of the fluid domain
    fxDim = paramsH->fluidDimX + smalldis;
    fyDim = paramsH->fluidDimY + smalldis;
    fzDim = paramsH->fluidDimZ + smalldis;

    sphere_radius = paramsH->bodyRad;

    myFsiSystem.SetFluidDynamics(paramsH->fluid_dynamic_type);
    myFsiSystem.SetFluidSystemLinearSolver(paramsH->LinearSolver);

    // fsi::utils::ParseJSON sets default values to cMin and cMax which may need
    // to be modified depending on the case (e.g periodic BC)
    Real initSpace0 = paramsH->MULT_INITSPACE * paramsH->HSML;
    paramsH->cMin = fsi::mR3(-bxDim / 2, -byDim / 2, -bzDim / 2 - 5 * initSpace0) * 2 - 4 * initSpace0;
    paramsH->cMax = fsi::mR3( bxDim / 2,  byDim / 2,  bzDim / 1 + 5 * initSpace0) * 2 + 4 * initSpace0;
    // call FinalizeDomainCreating to setup the binning for neighbor search or write your own
    fsi::utils::FinalizeDomain(paramsH);
    fsi::utils::PrepareOutputDir(paramsH, demo_dir, out_dir, argv[1]);

    // ******************************* Create Fluid region ****************************************
    /// Create an initial box of fluid
    utils::GridSampler<> sampler(initSpace0);
    /// Use a chrono sampler to create a bucket of fluid
    ChVector<> boxCenter(0, 0, fzDim / 2 + 0 * initSpace0);
    ChVector<> boxHalfDim(fxDim / 2, fyDim / 2, fzDim / 2);
    utils::Generator::PointVector points = sampler.SampleBox(boxCenter, boxHalfDim);
    /// Add fluid markers from the sampler points to the FSI system
    size_t numPart = points.size();
    for (int i = 0; i < numPart; i++) {
        Real pre_ini = paramsH->rho0 * abs(paramsH->gravity.z) * (-points[i].z() + fzDim);
        Real rho_ini = paramsH->rho0 + pre_ini / (paramsH->Cs * paramsH->Cs);
        myFsiSystem.GetDataManager()->AddSphMarker(
            fsi::mR4(points[i].x(), points[i].y(), points[i].z(), paramsH->HSML), fsi::mR3(0.0, 0.0, 0.0),
            fsi::mR4(paramsH->rho0, pre_ini, paramsH->mu0, -1),  // initial presssure modified as 0.0
            fsi::mR3(0.0e0),                               // tauxxyyzz
            fsi::mR3(0.0e0));                              // tauxyxzyz
    }

    size_t numPhases = myFsiSystem.GetDataManager()->fsiGeneralData->referenceArray.size();

    if (numPhases != 0) {
        std::cout << "Error! numPhases is wrong, thrown from main\n" << std::endl;
        std::cin.get();
        return -1;
    } else {
        myFsiSystem.GetDataManager()->fsiGeneralData->referenceArray.push_back(mI4(0, numPart, -1, -1));
        myFsiSystem.GetDataManager()->fsiGeneralData->referenceArray.push_back(mI4(numPart, numPart, 0, 0));
    }

    /// Create MBD or FE model
    CreateSolidPhase(mphysicalSystem, myFsiSystem, paramsH);
    /// Construction of the FSI system must be finalized
    myFsiSystem.Finalize();

    /// Get the body from the FSI system for visualization
    double mTime = 0;
    int stepEnd = int(paramsH->tFinal / paramsH->dT);
    stepEnd = 1000000;

    /// use the following to write a VTK file of the sphere
    std::vector<std::vector<double>> vCoor;
    std::vector<std::vector<int>> faces;
    std::string RigidConectivity = demo_dir + "RigidConectivity.vtk";

    /// Set up integrator for the MBD
    mphysicalSystem.SetTimestepperType(ChTimestepper::Type::HHT);
    auto mystepper = std::static_pointer_cast<ChTimestepperHHT>(mphysicalSystem.GetTimestepper());
    mystepper->SetAlpha(-0.2);
    mystepper->SetMaxiters(1000);
    mystepper->SetAbsTolerances(1e-6);
    mystepper->SetMode(ChTimestepperHHT::ACCELERATION);
    mystepper->SetScaling(true);

    /// Get the body from the FSI system
    std::vector<std::shared_ptr<ChBody>>& FSI_Bodies = myFsiSystem.GetFsiBodies();
    auto Sphere = FSI_Bodies[0];
    SaveParaViewFiles(myFsiSystem, mphysicalSystem, paramsH, 0, 0);

    /// write the Penetration into file
    std::ofstream myFile;
    myFile.open("./Sphere_penetration_depth.txt", std::ios::trunc);
    myFile.close();
    myFile.open("./Sphere_penetration_depth.txt", std::ios::app);
    myFile << 0.0 << "\t" << 0.0 << "\t"
           << Sphere->GetPos().x() << "\t" << Sphere->GetPos().y() << "\t"<< Sphere->GetPos().z() << "\t"
           << Sphere->GetPos_dt().x() << "\t" << Sphere->GetPos_dt().y() << "\t"<< Sphere->GetPos_dt().z() << "\n";
    myFile.close();

    Real time = 0;
    Real Global_max_dT = paramsH->dT_Max;
    for (int tStep = 0; tStep < stepEnd + 1; tStep++) {
        printf("\nstep : %d, time= : %f (s) \n", tStep, time);
        double frame_time = 1.0 / paramsH->out_fps;
        int next_frame = (int)floor((time + 1e-6) / frame_time) + 1;
        double next_frame_time = next_frame * frame_time;
        double max_allowable_dt = next_frame_time - time;
        if (max_allowable_dt > 1e-7)
            paramsH->dT_Max = std::min(Global_max_dT, max_allowable_dt);
        else
            paramsH->dT_Max = Global_max_dT;

        myFsiSystem.DoStepDynamics_FSI();
        time += paramsH->dT;
        SaveParaViewFiles(myFsiSystem, mphysicalSystem, paramsH, next_frame, time);

        auto bin = mphysicalSystem.Get_bodylist()[0];
        auto sphere = mphysicalSystem.Get_bodylist()[1];

        printf("bin=%f,%f,%f\n", bin->GetPos().x(), bin->GetPos().y(), bin->GetPos().z());
        printf("sphere=%f,%f,%f\n", sphere->GetPos().x(), sphere->GetPos().y(), sphere->GetPos().z());
        printf("sphere=%f,%f,%f\n", sphere->GetPos_dt().x(), sphere->GetPos_dt().y(), sphere->GetPos_dt().z());
        Real d_pen = fzDim + sphere_radius  + 0.5*initSpace0 - sphere->GetPos().z();
        // Real fy = d_pen/pow((0.1 + d_pen), 1.0/3.0);
        printf("sphere penetration = %f\n", d_pen);
        myFile.open("./Sphere_penetration_depth.txt", std::ios::app);
        myFile << time << "\t" << d_pen << "\t"
               << sphere->GetPos().x() << "\t" << sphere->GetPos().y() << "\t"<< sphere->GetPos().z() << "\t"
               << sphere->GetPos_dt().x() << "\t" << sphere->GetPos_dt().y() << "\t"<< sphere->GetPos_dt().z() << "\n";
        myFile.close();

        if (time > paramsH->tFinal)
            break;
    }

    return 0;
}

//------------------------------------------------------------------
// Create the objects of the MBD system. Rigid bodies, and if fsi, their
// bce representation are created and added to the systems
//------------------------------------------------------------------
void CreateSolidPhase(ChSystemSMC& mphysicalSystem,
                      fsi::ChSystemFsi& myFsiSystem,
                      std::shared_ptr<fsi::SimParams> paramsH) {
    Real initSpace0 = paramsH->MULT_INITSPACE * paramsH->HSML;

    ChVector<> gravity = ChVector<>(paramsH->gravity.x, paramsH->gravity.y, paramsH->gravity.z);
    mphysicalSystem.Set_G_acc(gravity);

    auto mysurfmaterial = chrono_types::make_shared<ChMaterialSurfaceSMC>();
    /// Set common material Properties
    mysurfmaterial->SetYoungModulus(1e8);
    mysurfmaterial->SetFriction(0.2f);
    mysurfmaterial->SetRestitution(0.05f);
    mysurfmaterial->SetAdhesion(0);

    /// Bottom wall
    ChVector<> sizeBottom(bxDim / 2 + 3 * initSpace0, byDim / 2 + 3 * initSpace0, 2 * initSpace0);
    ChVector<> posBottom(0, 0, -3 * initSpace0);
    ChVector<> posTop(0, 0, bzDim + 2 * initSpace0);

    /// left and right Wall
    ChVector<> size_YZ(2 * initSpace0, byDim / 2 + 3 * initSpace0, bzDim / 2);
    ChVector<> pos_xp(bxDim / 2 + initSpace0, 0.0, bzDim / 2 + 0 * initSpace0);
    ChVector<> pos_xn(-bxDim / 2 - 3 * initSpace0, 0.0, bzDim / 2 + 0 * initSpace0);

    /// Front and back Wall
    ChVector<> size_XZ(bxDim / 2, 2 * initSpace0, bzDim / 2);
    ChVector<> pos_yp(0, byDim / 2 + initSpace0, bzDim / 2 + 0 * initSpace0);
    ChVector<> pos_yn(0, -byDim / 2 - 3 * initSpace0, bzDim / 2 + 0 * initSpace0);

    /// Create a container
    auto bin = chrono_types::make_shared<ChBody>();
    bin->SetPos(ChVector<>(0.0, 0.0, 0.0));
    bin->SetRot(ChQuaternion<>(1, 0, 0, 0));
    bin->SetIdentifier(-1);
    bin->SetBodyFixed(true);
    bin->GetCollisionModel()->ClearModel();
    bin->GetCollisionModel()->SetSafeMargin(initSpace0 / 2);
    /// MBD representation of the walls
    AddWall(mysurfmaterial, bin, sizeBottom, posBottom);
    // AddWall(mysurfmaterial, bin, sizeBottom, posTop + ChVector<>(0.0, 0.0, 3 * initSpace0));
    AddWall(mysurfmaterial, bin, size_YZ, pos_xp);
    AddWall(mysurfmaterial, bin, size_YZ, pos_xn);
    AddWall(mysurfmaterial, bin, size_XZ, pos_yp + ChVector<>(+1.5 * initSpace0, +1.5 * initSpace0, 0.0));
    AddWall(mysurfmaterial, bin, size_XZ, pos_yn + ChVector<>(-0.5 * initSpace0, -0.5 * initSpace0, 0.0));
    bin->GetCollisionModel()->BuildModel();

    bin->SetCollide(false);
    mphysicalSystem.AddBody(bin);

    /// Fluid-Solid Coupling at the walls via Condition Enforcement (BCE) Markers
    fsi::utils::AddBoxBce(myFsiSystem.GetDataManager(), paramsH, bin, posBottom, QUNIT, sizeBottom);
    // fsi::utils::AddBoxBce(myFsiSystem.GetDataManager(), paramsH, bin, posTop, QUNIT, sizeBottom);
    fsi::utils::AddBoxBce(myFsiSystem.GetDataManager(), paramsH, bin, pos_xp, QUNIT, size_YZ, 23);
    fsi::utils::AddBoxBce(myFsiSystem.GetDataManager(), paramsH, bin, pos_xn, QUNIT, size_YZ, 23);
    fsi::utils::AddBoxBce(myFsiSystem.GetDataManager(), paramsH, bin, pos_yp, QUNIT, size_XZ, 13);
    fsi::utils::AddBoxBce(myFsiSystem.GetDataManager(), paramsH, bin, pos_yn, QUNIT, size_XZ, 13);

    /// Create falling sphere
    ChVector<> sphere_pos = ChVector<>(paramsH->bodyIniPosX, paramsH->bodyIniPosY, fzDim + 1.0 * sphere_radius  + 0.5 * initSpace0);//paramsH->bodyIniPosZ
    ChVector<> sphere_vel = ChVector<>(0.0, 0.0, -sqrt(abs(2.0*paramsH->gravity.z * paramsH->bodyIniPosZ)));
    ChQuaternion<> sphere_rot = QUNIT;
    auto sphere = chrono_types::make_shared<ChBody>();
    sphere->SetPos(sphere_pos);
    sphere->SetPos_dt(sphere_vel);
    double volume = utils::CalcSphereVolume(sphere_radius);
    ChVector<> gyration = utils::CalcSphereGyration(sphere_radius).diagonal();
    double density = paramsH->bodyDensity;//paramsH->rho0 * 0.5;
    double mass = density * volume;
    sphere->SetCollide(true);
    sphere->SetBodyFixed(false);

    sphere->GetCollisionModel()->ClearModel();
    sphere->GetCollisionModel()->SetSafeMargin(initSpace0);
    utils::AddSphereGeometry(sphere.get(), mysurfmaterial, sphere_radius, ChVector<>(0.0, 0.0, 0.0), ChQuaternion<>(1, 0, 0, 0));
    sphere->GetCollisionModel()->BuildModel();
    size_t numRigidObjects = mphysicalSystem.Get_bodylist().size();
    mphysicalSystem.AddBody(sphere);

    /// Add this body to the FSI system
    myFsiSystem.AddFsiBody(sphere);
    /// Fluid-Solid Coupling of the sphere via Condition Enforcement (BCE) Markers
    fsi::utils::AddSphereBce(myFsiSystem.GetDataManager(), paramsH, sphere, ChVector<>(0, 0, 0), ChQuaternion<>(1, 0, 0, 0),
                           sphere_radius);

    double FSI_MASS = myFsiSystem.GetDataManager()->numObjects->numRigid_SphMarkers * paramsH->markerMass;
    //    sphere->SetMass(FSI_MASS);
    sphere->SetMass(mass);
    sphere->SetInertiaXX(mass * gyration);
    printf("inertia=%f,%f,%f\n", mass * gyration.x(), mass * gyration.y(), mass * gyration.z());
    printf("\nreal mass=%f, FSI_MASS=%f\n\n", mass, FSI_MASS);
}
//------------------------------------------------------------------
// Function to save the povray files of the MBD
//------------------------------------------------------------------
void SaveParaViewFiles(fsi::ChSystemFsi& myFsiSystem,
                       ChSystemSMC& mphysicalSystem,
                       std::shared_ptr<fsi::SimParams> paramsH,
                       int next_frame,
                       double mTime) {
    int out_steps = (int)ceil((1.0 / paramsH->dT) / paramsH->out_fps);
    int num_contacts = mphysicalSystem.GetNcontacts();
    double frame_time = 1.0 / paramsH->out_fps;
    static int out_frame = 0;

    if (pv_output && std::abs(mTime - (next_frame)*frame_time) < 1e-7) {
        fsi::utils::PrintToFile(myFsiSystem.GetDataManager()->sphMarkersD2->posRadD,
                                myFsiSystem.GetDataManager()->sphMarkersD2->velMasD,
                                myFsiSystem.GetDataManager()->sphMarkersD2->rhoPresMuD,
                                myFsiSystem.GetDataManager()->fsiGeneralData->sr_tau_I_mu_i,
                                myFsiSystem.GetDataManager()->fsiGeneralData->referenceArray,
                                myFsiSystem.GetDataManager()->fsiGeneralData->referenceArray_FEA, demo_dir, true);

        cout << "-------------------------------------\n" << endl;
        cout << "             Output frame:   " << next_frame << endl;
        cout << "             Time:           " << mTime << endl;
        cout << "-------------------------------------\n" << endl;

        out_frame++;
    }
}
