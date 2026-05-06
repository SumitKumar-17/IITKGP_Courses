import numpy as np
import os, sys, warnings
warnings.filterwarnings('ignore')

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from Assignment1_22CS30056_classifier_taskB import FairLogisticRegressionA
from Assignment1_22CS30056_classifier_taskC import FairLogisticRegressionB

DATA_DIR     = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data')
INTERP_NAMES = ['age', 'balance', 'housing', 'education', 'marital', 'loan']
N_INTERP     = 6
SIGMA        = np.sqrt(N_INTERP)
N_PERTURB    = 10
RANDOM_SEED  = 42
K_SPARSE     = 3
B_PICK       = 10


def load_models():
    theta_acc  = np.load(os.path.join(DATA_DIR, 'model_acc_theta.npy'))
    theta_fair = np.load(os.path.join(DATA_DIR, 'model_fair_theta.npy'))
    model_acc        = FairLogisticRegressionA(c=0.8)
    model_acc.theta  = theta_acc
    model_fair       = FairLogisticRegressionB(gamma=1.0)
    model_fair.theta = theta_fair
    return model_acc, model_fair


def perturb(x_interp, n=N_PERTURB, seed=None):
    rng  = np.random.RandomState(seed)
    flip = rng.randint(0, 2, size=(n, N_INTERP))
    z    = np.where(flip == 1, 1.0 - x_interp, x_interp)
    return np.vstack([x_interp.reshape(1, -1), z])


def reconstruct(Z_all, orig_embed, norm_mean, norm_std):
    n   = Z_all.shape[0]
    rec = np.tile(orig_embed, (n, 1))
    rec[:, :N_INTERP] = (Z_all - norm_mean[:N_INTERP]) / norm_std[:N_INTERP]
    return rec


def lime_coefficients(x_interp, Z_all, proba):
    d     = np.sum(x_interp != Z_all, axis=1).astype(float)
    w     = np.exp(-(d ** 2) / (SIGMA ** 2))
    n     = Z_all.shape[0]
    Z_aug = np.hstack([Z_all, np.ones((n, 1))])
    sq_w  = np.sqrt(w)
    Z_w   = Z_aug * sq_w[:, None]
    f_w   = proba  * sq_w
    theta, _, _, _ = np.linalg.lstsq(Z_w, f_w, rcond=None)
    return np.abs(theta[:N_INTERP])


def sparsify(coefs, k=K_SPARSE):
    sparse = np.zeros_like(coefs)
    top_k  = np.argsort(coefs)[::-1][:k]
    sparse[top_k] = coefs[top_k]
    return sparse


def sp_lime(W_sparse, I, B=B_PICK):
    n_inst, n_feat = W_sparse.shape
    picked         = []
    gains          = []
    covered        = np.zeros(n_feat, dtype=bool)

    for t in range(min(B, n_inst)):
        best_gain = -np.inf
        best_i    = -1

        for i in range(n_inst):
            if i in picked:
                continue
            new_feat = (W_sparse[i] > 0) & (~covered)
            gain     = float(np.sum(I[new_feat]))
            if gain > best_gain:
                best_gain = gain
                best_i    = i

        picked.append(best_i)
        gains.append(best_gain)
        covered |= (W_sparse[best_i] > 0)

        print(f"    Step {t+1:2d}: instance {best_i:5d} selected  "
              f"(gain = {best_gain:.4f},  "
              f"features covered so far: "
              f"{[INTERP_NAMES[j] for j in np.where(covered)[0]]})")

        if covered.all():
            print(f"           All {n_feat} features covered - stopping.")
            break

    return picked, gains


def main():
    print("Assignment 2  |  Task Z: Submodular Optimization  |  Roll: 22CS30056")
    print("=" * 68)
    print(f"\nAlgorithm: SP-LIME (Algorithm 2, Ribeiro et al. 2016)")
    print(f"Sparsity K = {K_SPARSE} features per instance  |  Pick budget B = {B_PICK}  |  σ = {SIGMA:.4f}\n")

    norm      = np.load(os.path.join(DATA_DIR, 'norm_params.npz'))
    norm_mean = norm['mean']
    norm_std  = norm['std']

    td        = np.load(os.path.join(DATA_DIR, 'test_set.npz'))
    Xi_te     = td['X_interp']
    X_te_norm = td['X_embed_norm']
    n_test    = Xi_te.shape[0]

    print(f"Test-set size : {n_test} instances")
    print("Running LIME on all test instances ...")

    model_acc, model_fair = load_models()

    W_full_acc  = np.zeros((n_test, N_INTERP))
    W_full_fair = np.zeros((n_test, N_INTERP))
    W_sp_acc    = np.zeros((n_test, N_INTERP))
    W_sp_fair   = np.zeros((n_test, N_INTERP))

    CHUNK = 500
    for start in range(0, n_test, CHUNK):
        end = min(start + CHUNK, n_test)
        for i in range(start, end):
            Z_all = perturb(Xi_te[i], N_PERTURB, seed=RANDOM_SEED + i)
            X_rec = reconstruct(Z_all, X_te_norm[i], norm_mean, norm_std)

            coef_acc  = lime_coefficients(Xi_te[i], Z_all, model_acc.predict_proba(X_rec))
            coef_fair = lime_coefficients(Xi_te[i], Z_all, model_fair.predict_proba(X_rec))

            W_full_acc[i]  = coef_acc
            W_full_fair[i] = coef_fair
            W_sp_acc[i]    = sparsify(coef_acc,  K_SPARSE)
            W_sp_fair[i]   = sparsify(coef_fair, K_SPARSE)

        print(f"  {end:>5}/{n_test}  ({100*end/n_test:.1f}%)", end='\r', flush=True)

    print(f"\n\nExplanation matrix (full) shape : {W_full_acc.shape}")
    print(f"Sparse matrix (top-{K_SPARSE}/instance) shape : {W_sp_acc.shape}")

    I_acc  = np.sqrt(W_full_acc.sum(axis=0))
    I_fair = np.sqrt(W_full_fair.sum(axis=0))

    print("\nGlobal feature importance  I_j = sqrt(Σ_i W[i,j]):")
    print(f"  {'Feature':<12} {'Model-Accurate':>16} {'Model-Fair':>12}  {'Rank-Acc':>10}")
    print(f"  {'-'*54}")
    rank_acc = np.argsort(I_acc)[::-1]
    for r, j in enumerate(rank_acc):
        print(f"  {INTERP_NAMES[j]:<12} {I_acc[j]:>16.4f} {I_fair[j]:>12.4f}  {r+1:>10}")

    print("\n" + "-" * 68)
    print(f"SP-LIME greedy pick  (B = {B_PICK} instances):")

    print(f"\n  Model-Accurate  (c=0.8):  [K-sparse, top-{K_SPARSE} per instance]")
    picked_acc,  gains_acc  = sp_lime(W_sp_acc,  I_acc,  B_PICK)

    print(f"\n  Model-Fair      (gamma=1.0):  [K-sparse, top-{K_SPARSE} per instance]")
    picked_fair, gains_fair = sp_lime(W_sp_fair, I_fair, B_PICK)

    def top3_from_picked(picked, W_sp, I, label):
        agg   = W_sp[picked].sum(axis=0)
        score = agg * I
        order = np.argsort(score)[::-1][:3]
        print(f"\n  {label}")
        print(f"    {'Rank':<6} {'Feature':<14} {'Agg-coef':>12} {'I_j':>10}")
        print(f"    {'-'*44}")
        for rank, j in enumerate(order):
            print(f"    {rank+1:<6} {INTERP_NAMES[j]:<14} {agg[j]:>12.4f} {I[j]:>10.4f}")
        return [INTERP_NAMES[j] for j in order]

    print("\n" + "-" * 68)
    print("Top-3 features from SP-LIME picked instances:")
    top3_acc  = top3_from_picked(picked_acc,  W_sp_acc,  I_acc,  "Model-Accurate (c=0.8)")
    top3_fair = top3_from_picked(picked_fair, W_sp_fair, I_fair, "Model-Fair     (gamma=1.0)")

    top3_global_acc  = [INTERP_NAMES[j] for j in np.argsort(I_acc)[::-1][:3]]
    top3_global_fair = [INTERP_NAMES[j] for j in np.argsort(I_fair)[::-1][:3]]

    print("\n" + "=" * 68)
    print("Top-3 features  (from SP-LIME picked instances):")
    print(f"  Model-Accurate : {top3_acc}")
    print(f"  Model-Fair     : {top3_fair}")
    print("\nTop-3 features  (global I_j ranking across all test instances):")
    print(f"  Model-Accurate : {top3_global_acc}")
    print(f"  Model-Fair     : {top3_global_fair}")

    print("\nFairness check - is 'age' (sensitive attribute) in top-3?")
    print(f"  Model-Accurate (SP-LIME) : {'age' in top3_acc}")
    print(f"  Model-Fair     (SP-LIME) : {'age' in top3_fair}")
    print(f"  Model-Accurate (global)  : {'age' in top3_global_acc}")
    print(f"  Model-Fair     (global)  : {'age' in top3_global_fair}")

    np.savez(os.path.join(DATA_DIR, 'submodular_results.npz'),
             W_full_acc    = W_full_acc,
             W_full_fair   = W_full_fair,
             W_sp_acc      = W_sp_acc,
             W_sp_fair     = W_sp_fair,
             I_acc         = I_acc,
             I_fair        = I_fair,
             picked_acc    = np.array(picked_acc,  dtype=int),
             picked_fair   = np.array(picked_fair, dtype=int),
             top3_sp_acc   = np.array([INTERP_NAMES.index(f) for f in top3_acc],  dtype=int),
             top3_sp_fair  = np.array([INTERP_NAMES.index(f) for f in top3_fair], dtype=int),
             top3_glob_acc = np.array([INTERP_NAMES.index(f) for f in top3_global_acc],  dtype=int),
             top3_glob_fair= np.array([INTERP_NAMES.index(f) for f in top3_global_fair], dtype=int))

    print(f"\nResults saved to {DATA_DIR}/submodular_results.npz")
    print("Task Z (Submodular Optimization) complete.")


if __name__ == "__main__":
    main()
