import sys
import pathlib as pl
src_folder = pl.Path('tests/timing/')
sys.path.append(str(src_folder))

import numpy as np
import pickle

import itertools
from multiprocessing import Pool
from four_link_timing import four_link

# For 'production'
step_sizes = np.array([1e-3, 2e-3, 4e-3, 8e-3, 1e-2, 2e-2, 4e-2, 8e-2, 1e-1])
M_vals = np.array([1e-8, 1e-9, 1e-10, 1e-11, 1e-12, 1e-13])

# # For testing
# step_sizes = np.array([2e-2, 4e-2, 8e-2])
# M_vals = np.array([1e-8, 1e-9])

end_time = 3
timing_runs = 5

dir_path = './output/timing/'

to_xyz = 'xyz'
pretty_form = {'rp': 'rp', 'rA': 'rA', 'reps': 'reps'}

ss, MM = np.meshgrid(step_sizes, M_vals)
with open(dir_path + 'mesh_params.pickle', 'wb') as handle:
    pickle.dump((ss, MM), handle, protocol=pickle.HIGHEST_PROTOCOL)

def run_model(args):
    form, model_fn = args

    pretty_name = '_'.join([word.capitalize() for word in model_fn.__name__.split('_')])

    timing = np.full((len(M_vals), len(step_sizes)), np.nan)

    for i, M in enumerate(M_vals):
        for j, step in enumerate(step_sizes):
            try:
                times = np.zeros(timing_runs)
                tol = M / step**2

                for k in range(0, timing_runs):
                    Δt = model_fn(['--form', form, '--mode', 'dynamics', '--tol', str(tol), '--step_size', str(step), '--end_time', str(end_time)])
                    times[k] = Δt

                timing[i, j] = np.mean(times) / end_time

            except RuntimeError:
                print('{}-{}, step: {}, tol: {} failed to converge'.format(form, pretty_name, str(step), str(tol)))

    save_name = '{}_{}_timing.pickle'.format(pretty_name, form)
    info = (pretty_name, pretty_form[form])

    with open(dir_path + save_name, 'wb') as handle:
        pickle.dump((info, timing), handle, protocol=pickle.HIGHEST_PROTOCOL)

    print('Completed {} {} Analysis'.format(pretty_name, pretty_form[form]))

tasks = []

# for model_fn in [single_pendulum, four_link, slider_crank]:
#     for form in ['rA', 'rp', 'reps']:
#         tasks.append((form, model_fn))

for model_fn in [four_link]:
    for form in ['rA', 'rp']:
        tasks.append((form, model_fn))

for task in tasks:
    run_model(task)

# pool = Pool()
# pool.map(run_model, tasks)
# pool.close()