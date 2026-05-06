import numpy as np
import os, sys, warnings
warnings.filterwarnings('ignore')

DATA_DIR     = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data')
INTERP_NAMES = ['age', 'balance', 'housing', 'education', 'marital', 'loan']
N_INTERP     = 6
SIGMA        = np.sqrt(N_INTERP)


def hamming_distance(x_orig, Z):
    return np.sum(x_orig != Z, axis=1).astype(float)


def lime_weights(x_orig, Z, sigma=SIGMA):
    d = hamming_distance(x_orig, Z)
    return np.exp(-(d ** 2) / (sigma ** 2))


def weighted_linear_regression(Z, f, weights):
    n     = Z.shape[0]
    Z_aug = np.hstack([Z, np.ones((n, 1))])
    sq_w  = np.sqrt(weights)
    Z_w   = Z_aug * sq_w[:, None]
    f_w   = f * sq_w
    theta, _, _, _ = np.linalg.lstsq(Z_w, f_w, rcond=None)
    return theta


def explain_instance(x_interp, Z_all, proba, sigma=SIGMA):
    weights   = lime_weights(x_interp, Z_all, sigma)
    theta     = weighted_linear_regression(Z_all, proba, weights)
    coefs     = theta[:N_INTERP]
    intercept = theta[N_INTERP]
    return coefs, intercept, weights


def main():
    print("Assignment 2  |  Task Z: LIME  |  Roll: 22CS30056")
    print("=" * 62)
    print(f"\nσ (bandwidth) = sqrt({N_INTERP}) = {SIGMA:.4f}")
    print(f"Interpretable features: {INTERP_NAMES}\n")

    selected = np.load(os.path.join(DATA_DIR, 'selected_indices.npy'))
    all_results = {}

    for k, idx in enumerate(selected):
        nb_path = os.path.join(DATA_DIR, f'neighborhood_{k}.npz')
        nb      = np.load(nb_path, allow_pickle=True)

        x_interp   = nb['x_interp']
        Z_all      = nb['Z_all']
        proba_acc  = nb['proba_acc']
        proba_fair = nb['proba_fair']
        y_true     = int(nb['y_true'][0])
        z_sens     = int(nb['z_sens'][0])

        print("-" * 62)
        print(f"Instance {k+1}  |  test_idx={idx}  |  y_true={y_true}  |  z (age≥39)={z_sens}")
        interp_str = ', '.join(f'{n}={int(v)}' for n, v in zip(INTERP_NAMES, x_interp))
        print(f"  x' = [{interp_str}]")

        inst_results = {}

        for label, proba in [("Model-Accurate (c=0.8)", proba_acc),
                              ("Model-Fair     (gamma=1.0)", proba_fair)]:

            coefs, intercept, weights = explain_instance(x_interp, Z_all, proba, SIGMA)

            print(f"\n  -- {label} --")
            print(f"     intercept = {intercept:.4f}")
            print(f"     {'Feature':<12} {'Coefficient':>14} {'|Coef|':>10}")
            print(f"     {'-'*38}")

            order = np.argsort(np.abs(coefs))[::-1]
            for j in order:
                print(f"     {INTERP_NAMES[j]:<12} {coefs[j]:>14.4f} {abs(coefs[j]):>10.4f}")

            pred_local = float(x_interp @ coefs + intercept)
            print(f"\n     Local model  at x' : {pred_local:.4f}")
            print(f"     Black-box    at x' : {float(proba[0]):.4f}")
            print(f"     delta                  : {abs(pred_local - proba[0]):.4f}")

            inst_results[label] = {
                'coefficients': coefs.tolist(),
                'intercept'   : float(intercept),
            }

        all_results[f'instance_{k}_idx{idx}'] = inst_results

    print("\n" + "=" * 62)

    print("\nSummary — top locally-important feature per instance")
    print(f"  {'Instance':>10} {'Model':>26} {'Top feature':>15}")
    print(f"  {'-'*55}")
    for k, idx in enumerate(selected):
        key = f'instance_{k}_idx{idx}'
        for label in ["Model-Accurate (c=0.8)", "Model-Fair     (gamma=1.0)"]:
            coefs = np.array(all_results[key][label]['coefficients'])
            top_j = int(np.argmax(np.abs(coefs)))
            print(f"  {k+1:>10} {label:>26} {INTERP_NAMES[top_j]:>15}")

    np.save(os.path.join(DATA_DIR, 'lime_5_results.npy'), all_results, allow_pickle=True)
    print(f"\nSaved LIME results to {DATA_DIR}/lime_5_results.npy")
    print("Task Z (LIME) complete.")


if __name__ == "__main__":
    main()
