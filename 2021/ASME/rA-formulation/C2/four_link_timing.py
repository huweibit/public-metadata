import logging
import argparse as arg
from collections import defaultdict
from time import process_time
from copy import copy

import numpy as np
import sympy as sp
import matplotlib.pyplot as plt

from system_ra import SystemRA
from system_rp import SystemRP
from system_reps import SystemREps
from physics import R, Y_AXIS, Z_AXIS
from tools import profiler, plot_many_kinematics, print_profiling, standard_setup

def four_link(args):

    π = np.pi

    # Set up command-line options
    parser = arg.ArgumentParser(description='Simulation of Haug\'s four-link mechanism')
    parser.add_argument('-t', '--end_time', type=float, default=3, dest='t_end')

    model_files = defaultdict(lambda: 'models/four_link_rotated.mdl')

    # Get system and change some settings
    sys, params = standard_setup(parser, model_files, args)
    sys.set_g_acc(-9.81 * Z_AXIS)
    sys.h = params.h
    sys.tol = params.tol
    sys.solver_order = 1

    t_grid = np.arange(0, params.t_end, params.h)

    # See Haug p. 459 for properties
    # Link 1
    sys.bodies[0].m = 2
    sys.bodies[0].J = np.diag([4, 2, 0])

    # Link 2
    sys.bodies[1].m = 1
    sys.bodies[1].J = np.diag([12.4, 0.01, 0])

    # Link 3
    sys.bodies[2].m = 1
    sys.bodies[2].J = np.diag([4.54, 0.01, 0])

    # Create driving constraint function and alternate function to swap to
    t = sp.symbols('t')
    ang_sym = π * t + π/2
    ang_alt = ang_sym - π/2

    con_num = len(sys.g_cons.cons) - 1

    # Set driving constraint properties, create alternate constraint function
    sys.g_cons.cons[-1].set_constraint_fn(sp.cos(ang_sym), t)
    alt_dp1 = copy(sys.g_cons.cons[-1])
    alt_dp1.set_constraint_fn(sp.cos(ang_alt), t)
    alt_dp1.aj = Z_AXIS

    sys.initialize()

    start = process_time()
    for i, t in enumerate(t_grid):
        # (Hack) swap g-cons to avoid driving constraint singularity
        if np.abs(np.abs(sys.g_cons.cons[con_num].f(t)) - 1) < 0.1:
            logging.info('Swapped g-con at time {:.3f}'.format(t))
            sys.g_cons.cons[con_num], alt_dp1 = alt_dp1, sys.g_cons.cons[con_num]

        sys.do_step(i, t)
    Δt = process_time() - start

    return Δt
