# Task B: Classifier A - Maximizing Accuracy under Fairness Constraints
import numpy as np
from scipy.optimize import minimize
import os
import json

class FairLogisticRegressionA:
    def __init__(self, c=0.5, max_iter=1000, verbose=False):
        self.c = c
        self.max_iter = max_iter
        self.verbose = verbose
        self.theta = None
        self.n_features = None

    def sigmoid(self, z):
        z = np.clip(z, -500, 500)  #for avoid overflow
        return np.where(
            z >= 0,
            1 / (1 + np.exp(-z)),
            np.exp(z) / (1 + np.exp(z))
        )

    def negative_log_likelihood(self, theta, X, y):
        scores = X @ theta
        scores = np.clip(scores, -500, 500)  #for avoid overflow
        loss = np.where(
            y * scores >= 0,
            np.log(1 + np.exp(-y * scores)),
            -y * scores + np.log(1 + np.exp(y * scores))
        )
        return np.sum(loss)

    def gradient_negative_log_likelihood(self, theta, X, y):
        scores = X @ theta
        probs = self.sigmoid(y * scores)
        gradient = -X.T @ (y * (1 - probs))
        return gradient

    def fairness_constraint_upper(self, theta, X, z):
        N = len(z)
        z_centered = z - np.mean(z)
        covariance = (1 / N) * np.sum(z_centered * (X @ theta))
        return self.c - covariance

    def fairness_constraint_lower(self, theta, X, z):
        N = len(z)
        z_centered = z - np.mean(z)
        covariance = (1 / N) * np.sum(z_centered * (X @ theta))
        return covariance + self.c

    def fit(self, X, y, z):
        N, d = X.shape
        self.n_features = d
        X_bias = np.hstack([X, np.ones((N, 1))])
        self.n_features = d + 1
        theta_init = np.random.randn(self.n_features) * 0.01
        constraints = [
            {'type': 'ineq', 'fun': self.fairness_constraint_upper, 'args': (X_bias, z)},
            {'type': 'ineq', 'fun': self.fairness_constraint_lower, 'args': (X_bias, z)}
        ]

        result = minimize(
            fun=self.negative_log_likelihood,
            x0=theta_init,
            args=(X_bias, y),
            method='SLSQP',
            jac=self.gradient_negative_log_likelihood,
            constraints=constraints,
            options={'maxiter': self.max_iter, 'disp': self.verbose}
        )

        if not result.success and self.verbose:
            print(f"Warning: Optimization did not converge. Message: {result.message}")

        self.theta = result.x
        return self

    def predict_proba(self, X):
        if self.theta is None:
            raise ValueError("Model not fitted yet. Call fit() first.")

        N = X.shape[0]
        X_bias = np.hstack([X, np.ones((N, 1))])
        scores = X_bias @ self.theta
        probs = self.sigmoid(scores)
        return probs

    def predict(self, X):
        probs = self.predict_proba(X)
        return np.where(probs >= 0.5, 1, -1)

    def compute_covariance(self, X, z):
        N = X.shape[0]
        X_bias = np.hstack([X, np.ones((N, 1))])
        z_centered = z - np.mean(z)
        covariance = (1 / N) * np.sum(z_centered * (X_bias @ self.theta))
        return covariance

def compute_accuracy(y_true, y_pred):
    return np.mean(y_true == y_pred)

def compute_p_rule(y_pred, z):
    z0_idx = (z == 0)
    z1_idx = (z == 1)

    if np.sum(z0_idx) == 0 or np.sum(z1_idx) == 0:
        return 0.0

    p_y1_given_z0 = np.mean(y_pred[z0_idx] == 1)
    p_y1_given_z1 = np.mean(y_pred[z1_idx] == 1)

    if p_y1_given_z1 == 0:
        return 0.0 if p_y1_given_z0 == 0 else 100.0

    ratio = p_y1_given_z0 / p_y1_given_z1
    p_rule = min(ratio, 1 / ratio) * 100

    return p_rule


def load_dataset(file_path):
    data = np.load(file_path, allow_pickle=True)
    return {
        'features': np.array(data['features'], dtype=np.float64),
        'sensitive_attr': np.array(data['sensitive_attr'], dtype=np.int32),
        'labels': np.array(data['labels'], dtype=np.int32)
    }


def evaluate_classifier(clf, X_train, y_train, z_train, X_test, y_test, z_test):
    y_train_pred = clf.predict(X_train)
    train_acc = compute_accuracy(y_train, y_train_pred)
    train_p_rule = compute_p_rule(y_train_pred, z_train)
    train_cov = clf.compute_covariance(X_train, z_train)

    y_test_pred = clf.predict(X_test)
    test_acc = compute_accuracy(y_test, y_test_pred)
    test_p_rule = compute_p_rule(y_test_pred, z_test)
    test_cov = clf.compute_covariance(X_test, z_test)

    return {
        'train_accuracy': train_acc,
        'train_p_rule': train_p_rule,
        'train_covariance': train_cov,
        'test_accuracy': test_acc,
        'test_p_rule': test_p_rule,
        'test_covariance': test_cov
    }


def main():
    print("Assignment-1")
    print("Task B: Classifier A")
    print("Roll Number: 22CS30056")
    
    dataset_names = ['synthetic_pi', 'synthetic_pi_2', 'synthetic_pi_4','synthetic_pi_6', 'synthetic_pi_8', 'adult']
    dataset_labels = ['π', 'π/2', 'π/4', 'π/6', 'π/8', 'Adult']
    c_values = [0.8, 0.5, 0.2, 0.0]
    all_results = {}

    for idx, (dataset_name, dataset_label) in enumerate(zip(dataset_names, dataset_labels)):
        print(f"Dataset {idx+1}/6: {dataset_label}")
        
        train_path = f"data/{dataset_name}_train.npz"
        test_path = f"data/{dataset_name}_test.npz"

        if not os.path.exists(train_path):
            print(f"Error: {train_path} not found. Run data generator first.")
            continue

        train_data = load_dataset(train_path)
        test_data = load_dataset(test_path)

        X_train = train_data['features']
        y_train = train_data['labels']
        z_train = train_data['sensitive_attr']

        X_test = test_data['features']
        y_test = test_data['labels']
        z_test = test_data['sensitive_attr']

        print(f"Train: {X_train.shape[0]} samples, {X_train.shape[1]} features")
        print(f"Test:  {X_test.shape[0]} samples")
        dataset_results = {}

        for c in c_values:
            print(f"\n  Training with c = {c}")

            clf = FairLogisticRegressionA(c=c, max_iter=1000, verbose=False)
            clf.fit(X_train, y_train, z_train)
            results = evaluate_classifier(clf, X_train, y_train, z_train,X_test, y_test, z_test)
            dataset_results[f'c_{c}'] = results

            print(f"Train Acc: {results['train_accuracy']:.4f}, "
                  f"p%-rule: {results['train_p_rule']:.2f}%, "
                  f"Cov: {results['train_covariance']:.4f}")
            print(f" TestAcc: {results['test_accuracy']:.4f}, "
                  f"p%-rule: {results['test_p_rule']:.2f}%, "
                  f"Cov: {results['test_covariance']:.4f}")

        all_results[dataset_name] = dataset_results

    output_file = 'results_classifier_A.json'
    with open(output_file, 'w') as f:
        json.dump(all_results, f, indent=2)

    print(f"Results saved to: {output_file}")
    
    print("final summary of the code ran above")
    print(f"{'Dataset':<12} {'c':<6} {'Train Acc':<12} {'Test Acc':<12} {'Test p%-rule':<12}")
    
    for idx, dataset_name in enumerate(dataset_names):
        dataset_label = dataset_labels[idx]
        if dataset_name in all_results:
            for c in c_values:
                key = f'c_{c}'
                if key in all_results[dataset_name]:
                    res = all_results[dataset_name][key]
                    print(f"{dataset_label:<12} {c:<6} "
                          f"{res['train_accuracy']:<12.4f} "
                          f"{res['test_accuracy']:<12.4f} "
                          f"{res['test_p_rule']:<12.2f}")

    print("="*70)


if __name__ == "__main__":
    main()
