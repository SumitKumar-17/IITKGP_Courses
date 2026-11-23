import numpy as np
import pandas as pd
import os, sys, warnings
warnings.filterwarnings('ignore')

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from Assignment1_22CS30056_classifier_taskB import (
    FairLogisticRegressionA, compute_accuracy, compute_p_rule)
from Assignment1_22CS30056_classifier_taskC import FairLogisticRegressionB

DATA_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bank+marketing', 'bank', 'bank-full.csv')
DATA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'data')
INTERP_NAMES = ['age', 'balance', 'housing', 'education', 'marital', 'loan']
N_INTERP = 6
N_PERTURB = 10
RANDOM_SEED = 42


def load_and_preprocess(data_path=DATA_PATH):
    df = pd.read_csv(data_path, sep=';')
    print(f"Loaded {len(df)} rows, {df.shape[1]} columns.")

    y_binary = (df['y'] == 'yes').astype(int).values
    y = 2 * y_binary - 1
    z = (df['age'] >= 39).astype(int).values

    age_bin     = (df['age']     >= 39).astype(float).values
    balance_bin = (df['balance'] >= 448).astype(float).values
    housing_bin = (df['housing'] == 'yes').astype(float).values
    edu_bin     = df['education'].isin(['primary', 'secondary']).astype(float).values
    marital_bin = (df['marital'] == 'married').astype(float).values
    loan_bin    = (df['loan']    == 'yes').astype(float).values

    X_interp = np.column_stack([age_bin, balance_bin, housing_bin, edu_bin, marital_bin, loan_bin])

    num_cols = ['day', 'duration', 'campaign', 'pdays', 'previous']
    X_num_other = df[num_cols].values.astype(float)

    cat_cols = ['job', 'default', 'contact', 'month', 'poutcome']
    df_cat = pd.get_dummies(df[cat_cols], drop_first=True)
    X_cat = df_cat.values.astype(float)
    cat_col_names = df_cat.columns.tolist()

    X_embed = np.hstack([X_interp, X_num_other, X_cat])

    print(f"  Embedding dim            : {X_embed.shape[1]}")
    print(f"  Interpretable features   : {N_INTERP}")
    print(f"  Sensitive attr (z=0/z=1) : {np.bincount(z)}")
    print(f"  Labels (y=-1/y=+1)       : {np.unique(y, return_counts=True)[1]}")
    return X_embed, X_interp, z, y, y_binary, cat_col_names


def train_test_split_data(X_embed, X_interp, z, y, y_binary, test_size=0.2, seed=RANDOM_SEED):
    np.random.seed(seed)
    n = len(y)
    idx = np.random.permutation(n)
    n_test = int(n * test_size)
    te_idx = idx[:n_test]
    tr_idx = idx[n_test:]

    def s(a): return a[tr_idx], a[te_idx]

    X_tr,  X_te  = s(X_embed)
    Xi_tr, Xi_te = s(X_interp)
    z_tr,  z_te  = s(z)
    y_tr,  y_te  = s(y)
    yb_tr, yb_te = s(y_binary)
    return (X_tr, Xi_tr, z_tr, y_tr, yb_tr, tr_idx,
            X_te, Xi_te, z_te, y_te, yb_te, te_idx)


def normalize(X_tr, X_te):
    mean = X_tr.mean(axis=0)
    std  = X_tr.std(axis=0)
    std[std == 0] = 1.0
    return (X_tr - mean) / std, (X_te - mean) / std, mean, std


def train_models(X_tr_norm, y_tr, z_tr):
    print("\n  Training Model-Accurate  (Classifier A, c=0.8) ...")
    model_acc = FairLogisticRegressionA(c=0.8, max_iter=1000, verbose=False)
    model_acc.fit(X_tr_norm, y_tr, z_tr)
    print("  done.")

    print("  Training Model-Fair      (Classifier B, gamma=1.0) ...")
    model_fair = FairLogisticRegressionB(gamma=1.0, max_iter=1000, verbose=False)
    model_fair.fit(X_tr_norm, y_tr, z_tr)
    print("  done.")
    return model_acc, model_fair


def evaluate(model, name, X_tr_n, y_tr, z_tr, X_te_n, y_te, z_te):
    pred_tr = model.predict(X_tr_n)
    pred_te = model.predict(X_te_n)
    print(f"\n  {name}")
    print(f"    Train  Acc={compute_accuracy(y_tr, pred_tr):.4f} | "
          f"p%-rule={compute_p_rule(pred_tr, z_tr):.2f}%")
    print(f"    Test   Acc={compute_accuracy(y_te, pred_te):.4f} | "
          f"p%-rule={compute_p_rule(pred_te, z_te):.2f}% | "
          f"Cov={model.compute_covariance(X_te_n, z_te):.4f}")
    return pred_te


def select_instances(model_acc, model_fair, X_te_norm, y_te, z_te,
                     n_disagree=2, n_total=5, seed=RANDOM_SEED):
    pred_acc  = model_acc.predict(X_te_norm)
    pred_fair = model_fair.predict(X_te_norm)

    dis_mask = (pred_acc != pred_fair)
    agr_mask = ~dis_mask
    dis_idx  = np.where(dis_mask)[0]
    agr_idx  = np.where(agr_mask)[0]

    print(f"\n  Disagree on {len(dis_idx)} / {len(y_te)} test samples")
    print(f"  Agree    on {len(agr_idx)} / {len(y_te)} test samples")

    rng        = np.random.RandomState(seed)
    chosen_dis = rng.choice(dis_idx, min(n_disagree, len(dis_idx)), replace=False)
    n_agr_need = n_total - len(chosen_dis)
    chosen_agr = rng.choice(agr_idx, n_agr_need, replace=False)
    selected   = np.concatenate([chosen_dis, chosen_agr]).astype(int)

    print(f"\n  Selected 5 test-set indices: {selected.tolist()}")
    print(f"  {'idx':>6} {'z':>4} {'y_true':>8} {'pred_acc':>10} {'pred_fair':>10} {'agree':>7}")
    print(f"  {'-'*52}")
    for idx in selected:
        ag = "yes" if pred_acc[idx] == pred_fair[idx] else "NO"
        print(f"  {idx:>6} {z_te[idx]:>4} {y_te[idx]:>8} "
              f"{pred_acc[idx]:>10} {pred_fair[idx]:>10} {ag:>7}")
    return selected, pred_acc, pred_fair


def perturb(x_interp, n=N_PERTURB, seed=None):
    rng  = np.random.RandomState(seed)
    flip = rng.randint(0, 2, size=(n, N_INTERP))
    return np.where(flip == 1, 1.0 - x_interp, x_interp)


def reconstruct(Z_batch, orig_embed_norm, norm_mean, norm_std):
    n   = Z_batch.shape[0]
    rec = np.tile(orig_embed_norm, (n, 1))
    rec[:, :N_INTERP] = (Z_batch - norm_mean[:N_INTERP]) / norm_std[:N_INTERP]
    return rec


def generate_neighbourhoods(selected, Xi_te, X_te_norm, norm_mean, norm_std, model_acc, model_fair):
    neighbourhoods = {}
    for k, idx in enumerate(selected):
        x_interp = Xi_te[idx]
        x_embed  = X_te_norm[idx]

        Z_pert = perturb(x_interp, n=N_PERTURB, seed=RANDOM_SEED + k)
        Z_all  = np.vstack([x_interp.reshape(1, -1), Z_pert])

        X_rec      = reconstruct(Z_all, x_embed, norm_mean, norm_std)
        proba_acc  = model_acc.predict_proba(X_rec)
        proba_fair = model_fair.predict_proba(X_rec)

        neighbourhoods[int(idx)] = dict(
            x_interp   = x_interp,
            x_embed    = x_embed,
            Z_all      = Z_all,
            proba_acc  = proba_acc,
            proba_fair = proba_fair,
        )
        print(f"  Instance {k+1} (test_idx={idx:4d}) | "
              f"x'={x_interp.astype(int).tolist()} | "
              f"μ_prob_acc={proba_acc.mean():.3f}  "
              f"μ_prob_fair={proba_fair.mean():.3f}")
    return neighbourhoods


def save_data(selected, neighbourhoods, model_acc, model_fair,
              norm_mean, norm_std, Xi_te, X_te_norm, y_te, z_te):
    os.makedirs(DATA_DIR, exist_ok=True)

    np.savez(os.path.join(DATA_DIR, 'norm_params.npz'), mean=norm_mean, std=norm_std)
    np.save(os.path.join(DATA_DIR, 'model_acc_theta.npy'),  model_acc.theta)
    np.save(os.path.join(DATA_DIR, 'model_fair_theta.npy'), model_fair.theta)

    np.savez(os.path.join(DATA_DIR, 'test_set.npz'),
             X_interp=Xi_te, X_embed_norm=X_te_norm, y=y_te, z=z_te)

    np.save(os.path.join(DATA_DIR, 'selected_indices.npy'), np.array(selected, dtype=int))

    for k, idx in enumerate(selected):
        nb = neighbourhoods[int(idx)]
        np.savez(os.path.join(DATA_DIR, f'neighborhood_{k}.npz'),
                 idx        = np.array([idx]),
                 x_interp   = nb['x_interp'],
                 x_embed    = nb['x_embed'],
                 Z_all      = nb['Z_all'],
                 proba_acc  = nb['proba_acc'],
                 proba_fair = nb['proba_fair'],
                 y_true     = np.array([y_te[idx]]),
                 z_sens     = np.array([z_te[idx]]))

    print(f"\n  All data saved to  {DATA_DIR}/")


def main():
    print("Assignment 2  |  Task X & Task Y  |  Roll: 22CS30056")
    print("=" * 62)

    print("\n[1] Load & preprocess Bank Marketing dataset")
    X_embed, X_interp, z, y, y_binary, cat_col_names = load_and_preprocess()

    print("\n[2] Train/test split  (80 % train  /  20 % test)")
    (X_tr, Xi_tr, z_tr, y_tr, yb_tr, tr_idx,
     X_te, Xi_te, z_te, y_te, yb_te, te_idx) = train_test_split_data(
        X_embed, X_interp, z, y, y_binary)
    print(f"  Train: {X_tr.shape[0]}  |  Test: {X_te.shape[0]}  |  Embedding dim: {X_tr.shape[1]}")

    print("\n[3] Z-score normalisation (training statistics)")
    X_tr_n, X_te_n, norm_mean, norm_std = normalize(X_tr, X_te)

    print("\n[Task X] Train Model-Accurate & Model-Fair")
    model_acc, model_fair = train_models(X_tr_n, y_tr, z_tr)

    print("\n[Task X] Evaluation on train and test sets")
    evaluate(model_acc,  "Model-Accurate  (c=0.8)", X_tr_n, y_tr, z_tr, X_te_n, y_te, z_te)
    evaluate(model_fair, "Model-Fair      (gamma=1.0)", X_tr_n, y_tr, z_tr, X_te_n, y_te, z_te)

    print("\n[Task X] Select 5 instances from test set")
    selected, pred_acc, pred_fair = select_instances(model_acc, model_fair, X_te_n, y_te, z_te)

    print("\n[Task Y] Generate 10-sample neighbourhoods")
    neighbourhoods = generate_neighbourhoods(
        selected, Xi_te, X_te_n, norm_mean, norm_std, model_acc, model_fair)

    print("\n[Save] Writing data for Task Z ...")
    save_data(selected, neighbourhoods, model_acc, model_fair,
              norm_mean, norm_std, Xi_te, X_te_n, y_te, z_te)

    print("\n" + "=" * 62)
    print("Task X and Task Y complete.")


if __name__ == "__main__":
    main()
