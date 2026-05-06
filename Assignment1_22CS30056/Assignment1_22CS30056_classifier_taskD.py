# Task D: Checking Robustness with Label Noise
import numpy as np
from scipy.stats import multivariate_normal
import sys
import os
import warnings

warnings.filterwarnings('ignore', category=RuntimeWarning)
warnings.filterwarnings('ignore', category=DeprecationWarning)

sys.path.insert(0, os.path.dirname(__file__))
from Assignment1_22CS30056_classifier_taskB import FairLogisticRegressionA, compute_accuracy, compute_p_rule
from Assignment1_22CS30056_classifier_taskC import FairLogisticRegressionB
import json

class NoisyDataGenerator:
    def __init__(self, n_samples=10000, random_seed=42):
        self.n_samples = n_samples
        self.random_seed = random_seed
        np.random.seed(random_seed)

        self.mu_pos = np.array([1, -3])
        self.cov_pos = np.array([[3, 1], [1, 2]])
        self.mu_neg = np.array([-3, 2])
        self.cov_neg = np.array([[7, 1], [1, 10]])
        self.phi = np.pi / 4

    def compute_log_odds(self, features):
        log_odds = np.zeros(self.n_samples)

        for i, x in enumerate(features):
            p_x_given_y_pos = multivariate_normal.pdf(x, self.mu_pos, self.cov_pos)
            p_x_given_y_neg = multivariate_normal.pdf(x, self.mu_neg, self.cov_neg)
            log_odds[i] = np.log((p_x_given_y_pos + 1e-10) / (p_x_given_y_neg + 1e-10))

        return log_odds

    def apply_gaussian_noise(self, log_odds, sigma):
        epsilon = np.random.normal(0, sigma, size=self.n_samples)
        return log_odds + epsilon

    def generate_noisy_labels(self, noisy_log_odds):
        return np.where(noisy_log_odds >= 0, 1, -1)

    def rotate_features(self, features):
        rotation_matrix = np.array([
            [np.cos(self.phi), -np.sin(self.phi)],
            [np.sin(self.phi), np.cos(self.phi)]
        ])

        return features @ rotation_matrix.T

    def generate_sensitive_attribute(self, features_rotated):
        sensitive_attr = np.zeros(self.n_samples, dtype=int)

        for i, x_prime in enumerate(features_rotated):
            p_x_given_y_pos = multivariate_normal.pdf(x_prime, self.mu_pos, self.cov_pos)
            p_x_given_y_neg = multivariate_normal.pdf(x_prime, self.mu_neg, self.cov_neg)
            prob = p_x_given_y_pos / (p_x_given_y_pos + p_x_given_y_neg + 1e-10)
            sensitive_attr[i] = np.random.binomial(1, prob)

        return sensitive_attr

    def generate_noisy_dataset(self, sigma, n_samples=None):
        if n_samples is not None:
            self.n_samples = n_samples

        n_pos = self.n_samples // 2
        n_neg = self.n_samples - n_pos

        features_pos = np.random.multivariate_normal(self.mu_pos, self.cov_pos, n_pos)
        features_neg = np.random.multivariate_normal(self.mu_neg, self.cov_neg, n_neg)
        features = np.vstack([features_pos, features_neg])

        indices = np.random.permutation(self.n_samples)
        features = features[indices]
        log_odds = self.compute_log_odds(features)
        noisy_log_odds = self.apply_gaussian_noise(log_odds, sigma)
        noisy_labels = self.generate_noisy_labels(noisy_log_odds)
        features_rotated = self.rotate_features(features)
        sensitive_attr = self.generate_sensitive_attribute(features_rotated)

        dataset = {
            'features': features_rotated,
            'sensitive_attr': sensitive_attr,
            'labels': noisy_labels
        }

        return dataset

def split_dataset(dataset, test_size=0.2, random_seed=42):
    np.random.seed(random_seed)

    n_samples = len(dataset['labels'])
    n_test = int(n_samples * test_size)

    indices = np.random.permutation(n_samples)
    test_indices = indices[:n_test]
    train_indices = indices[n_test:]

    train_data = {
        'features': dataset['features'][train_indices],
        'sensitive_attr': dataset['sensitive_attr'][train_indices],
        'labels': dataset['labels'][train_indices]
    }

    test_data = {
        'features': dataset['features'][test_indices],
        'sensitive_attr': dataset['sensitive_attr'][test_indices],
        'labels': dataset['labels'][test_indices]
    }

    return train_data, test_data

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
    print("ASSIGNMENT 1 - FAIR CLASSIFICATION")
    print("Task D: Checking Robustness with Label Noise")
    print("Roll Number: 22CS30056")
    sigma_values = [0.4, 0.7]
    all_results = {}

    for sigma in sigma_values:
        print(f"Noise Level: sigma = {sigma}")
        print(f"\nGenerating noisy dataset with sigma = {sigma}")
        generator =NoisyDataGenerator(n_samples=10000,random_seed=42)
        dataset =generator.generate_noisy_dataset(sigma=sigma)

        print(f"Features shape: {dataset['features'].shape}")
        print(f"Sensitive attr (z=0/z=1): {np.bincount(dataset['sensitive_attr'])}")
        print(f"Noisy labels (y=-1/y=1): {np.unique(dataset['labels'], return_counts=True)[1]}")
        train_data, test_data = split_dataset(dataset, test_size=0.2, random_seed=42)
        X_train = train_data['features']
        y_train = train_data['labels']
        z_train = train_data['sensitive_attr']

        X_test = test_data['features']
        y_test = test_data['labels']
        z_test = test_data['sensitive_attr']
        print(f"\nTrain:{X_train.shape[0]} samples")
        print(f"Test: {X_test.shape[0]} samples")
        sigma_results = {}
        print(f"\nTraining Classifier A (c = 0.0)")
        clf_a = FairLogisticRegressionA(c=0.0, max_iter=1000, verbose=False)
        clf_a.fit(X_train, y_train, z_train)
        results_a = evaluate_classifier(clf_a, X_train, y_train, z_train,X_test, y_test, z_test)
        sigma_results['classifier_A'] = results_a

        print(f"Train Acc: {results_a['train_accuracy']:.4f}, "
              f"p%-rule:{results_a['train_p_rule']:.2f}%, "
              f"Cov:{results_a['train_covariance']:.4f}")
        print(f" TestAcc: {results_a['test_accuracy']:.4f}, "
              f"p%-rule: {results_a['test_p_rule']:.2f}%, "
              f"Cov: {results_a['test_covariance']:.4f}")

        print(f"\nTraining Classifier B (Y = 0.0)")
        clf_b = FairLogisticRegressionB(gamma=0.0, max_iter=1000, verbose=False)
        clf_b.fit(X_train, y_train, z_train)

        results_b = evaluate_classifier(clf_b, X_train, y_train, z_train,X_test, y_test, z_test)
        sigma_results['classifier_B'] = results_b

        print(f"Train Acc: {results_b['train_accuracy']:.4f}, "
              f"p%-rule: {results_b['train_p_rule']:.2f}%, "
              f"Cov: {results_b['train_covariance']:.4f}")
        print(f"Test  Acc: {results_b['test_accuracy']:.4f}, "
              f"p%-rule: {results_b['test_p_rule']:.2f}%, "
              f"Cov: {results_b['test_covariance']:.4f}")

        all_results[f'sigma_{sigma}'] = sigma_results

    output_file = 'results_task_D.json'
    with open(output_file, 'w') as f:
        json.dump(all_results, f, indent=2)

    print(f"Results saved to: {output_file}")
    print("summary table for the taskD")
    print(f"{'sigma':<8} {'Classifier':<15} {'Train Acc':<12} {'Test Acc':<12} {'Test p%-rule':<12}")
    
    for sigma in sigma_values:
        key = f'sigma_{sigma}'
        if key in all_results:
            # Classifier A
            res_a = all_results[key]['classifier_A']
            print(f"{sigma:<8} {'A (c=0.0)':<15} "
                  f"{res_a['train_accuracy']:<12.4f} "
                  f"{res_a['test_accuracy']:<12.4f} "
                  f"{res_a['test_p_rule']:<12.2f}")

            # Classifier B
            res_b = all_results[key]['classifier_B']
            print(f"{sigma:<8} {'B (gamma=0.0)':<15} "
                  f"{res_b['train_accuracy']:<12.4f} "
                  f"{res_b['test_accuracy']:<12.4f} "
                  f"{res_b['test_p_rule']:<12.2f}")

    print("="*70)


if __name__ == "__main__":
    main()
