import matplotlib.pyplot as plt
import argparse as arg
import logging

import cProfile
import pstats
import io
from pstats import SortKey

from system_ra import SystemRA
from system_rp import SystemRP
from system_reps import SystemREps

profiler = cProfile.Profile()

def standard_setup(parser, model_files, args = None):
    """
    Initializes common command-line options for multi-body simulation
    """

    # Setup command-line options
    parser.add_argument('--form', choices=['rp', 'rA', 'reps'], default='rp')
    parser.add_argument('--mode', choices=['kin', 'dyn', 'kinematics', 'dynamics'], default='kinematics')
    parser.add_argument('--tol', type=float)
    parser.add_argument('--step_size', type=float, default=1e-3, dest='h')

    parser.add_argument('-l', '--log', choices=['debug', 'info', 'warning', 'error'], default='info')
    parser.add_argument('-o', '--output')

    parser.add_argument('--plot', default=False, action='store_true')
    parser.add_argument('--no-plot', dest='plot', action='store_false')

    parser.add_argument('--save_data', default=False, action='store_true')
    parser.add_argument('--read_data', default=False, action='store_true')
    
    out_args = parser.parse_args() if args is None else parser.parse_args(args)

    # Determine which formulation to use
    if out_args.form == 'rp':
        sys = SystemRP.init_from_file(model_files['rp'])
    elif out_args.form == 'rA':
        sys = SystemRA.init_from_file(model_files['rA'])
    elif out_args.form == 'reps':
        sys = SystemREps.init_from_file(model_files['reps'])
    else:
        raise ValueError('Unmapped formulation {} encountered'.format(out_args.form))

    # Determine whether to do kinematics or dynamics
    if out_args.mode.startswith('kin'):
        sys.set_kinematics()
    elif out_args.mode.startswith('dyn'):
        sys.set_dynamics()
    else:
        raise ValueError('Unmapped mode {} encountered'.format(out_args.mode))

    logging.basicConfig(filename=out_args.output, level=getattr(logging, out_args.log.upper()), format='%(message)s')

    return sys, out_args

def plot_kinematics_analysis(grid, position, velocity, acceleration, title=''):
    """
    Creates a plot with three sub-plots showing the position, velocity and acceleration of a particular body
    """
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, sharex=True)
    fig.suptitle(title, fontsize=16)
    # O′ - position
    ax1.plot(grid, position[:, 0])
    ax1.plot(grid, position[:, 1])
    ax1.plot(grid, position[:, 2])
    ax1.set_title('Position of body')
    ax1.set_xlabel('t [s]')
    ax1.set_ylabel('Position [m]')

    # O′ - velocity
    ax2.plot(grid, velocity[:, 0])
    ax2.plot(grid, velocity[:, 1])
    ax2.plot(grid, velocity[:, 2])
    ax2.set_title('Velocity of body')
    ax2.set_xlabel('t [s]')
    ax2.set_ylabel('Velocity [m/s]')

    # O′ - acceleration
    ax3.plot(grid, acceleration[:, 0], label='x')
    ax3.plot(grid, acceleration[:, 1], label='y')
    ax3.plot(grid, acceleration[:, 2], label='z')
    ax3.set_title('Acceleration of body')
    ax3.set_xlabel('t [s]')
    ax3.set_ylabel('Acceleration [m/s²]')

    handles, labels = ax3.get_legend_handles_labels()
    fig.legend(handles, labels, loc='lower right')


def plot_many_kinematics(grid, O_poses, O_vels, O_accs, titles):
    """
    Repeatedly calls plot_kinematics_analysis
    """
    num_plots = len(O_poses)

    assert num_plots == len(O_vels) and num_plots == len(O_accs)

    for O_pos, O_vel, O_acc, title in zip(O_poses, O_vels, O_accs, titles):
        plot_kinematics_analysis(grid, O_pos, O_vel, O_acc, title)

    plt.show()


def print_profiling(profiler):
    """
    Prints out profiling information, based on suggestions here https://docs.python.org/3/library/profile.html#module-cProfile
    """
    s = io.StringIO()
    sortby = 'tottime'
    ps = pstats.Stats(profiler, stream=s).sort_stats(sortby)
    ps.print_stats()
    print(s.getvalue())