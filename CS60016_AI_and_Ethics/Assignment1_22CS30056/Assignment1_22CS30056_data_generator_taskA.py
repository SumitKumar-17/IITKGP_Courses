import numpy as np
import pandas as pd
from scipy.stats import multivariate_normal
import os
import sys

class SyntheticDataGenerator:
    def __init__(self, n_samples=10000, random_seed=42):
        self.n_samples = n_samples
        self.random_seed = random_seed
        np.random.seed(random_seed)

        self.mu_pos = np.array([1, -3])
        self.cov_pos = np.array([[3, 1], [1, 2]])
        self.mu_neg = np.array([-3, 2])
        self.cov_neg = np.array([[7, 1], [1, 10]])
        self.rotation_angles = [np.pi, np.pi/2, np.pi/4, np.pi/6, np.pi/8]

    def generate_labels(self):
        return np.random.choice([-1, 1], size=self.n_samples)

    def generate_features(self, labels):
        features = np.zeros((self.n_samples, 2))

        for i, y in enumerate(labels):
            if y == 1:
                features[i] = np.random.multivariate_normal(self.mu_pos, self.cov_pos)
            else:
                features[i] = np.random.multivariate_normal(self.mu_neg, self.cov_neg)

        return features

    def rotate_features(self, features, phi):
        rotation_matrix = np.array([
            [np.cos(phi), -np.sin(phi)],[np.sin(phi), np.cos(phi)]
        ])

        return features @ rotation_matrix.T

    def generate_sensitive_attribute(self, features_rotated, labels):
        sensitive_attr = np.zeros(self.n_samples, dtype=int)

        for i, x_prime in enumerate(features_rotated):
            p_x_given_y_pos = multivariate_normal.pdf(x_prime, self.mu_pos, self.cov_pos)
            p_x_given_y_neg = multivariate_normal.pdf(x_prime, self.mu_neg, self.cov_neg)
            prob = p_x_given_y_pos / (p_x_given_y_pos + p_x_given_y_neg + 1e-10)
            sensitive_attr[i] = np.random.binomial(1, prob)

        return sensitive_attr

    def generate_synthetic_dataset(self, phi, save_path=None):
        labels = self.generate_labels()
        features = self.generate_features(labels)
        features_rotated = self.rotate_features(features, phi)
        sensitive_attr = self.generate_sensitive_attribute(features_rotated, labels)

        dataset = {
            'features': features_rotated,'sensitive_attr': sensitive_attr,'labels': labels
        }

        if save_path:
            np.savez(save_path,features=features_rotated,sensitive_attr=sensitive_attr,labels=labels,phi=phi)
            print(f"Saved tothis locations: {save_path}")

        return dataset

    def generate_all_synthetic_datasets(self, output_dir='data'):
        os.makedirs(output_dir, exist_ok=True)

        datasets = []
        angle_names = ['pi', 'pi_2', 'pi_4', 'pi_6', 'pi_8']
        angle_labels = ['π', 'π/2', 'π/4', 'π/6', 'π/8']

        for i, phi in enumerate(self.rotation_angles):
            print(f"\n[{i+1}/5] Generating synthetic dataset with phi = {angle_labels[i]} ({phi:.4f} rad)")

            save_path = os.path.join(output_dir, f'synthetic_{angle_names[i]}.npz')
            dataset = self.generate_synthetic_dataset(phi, save_path)
            datasets.append(dataset)

            print(f"Features shape: {dataset['features'].shape}")
            print(f"Sensitive attr (z=0/z=1): {np.bincount(dataset['sensitive_attr'])}")
            print(f"Labels (y=-1/y=1): {np.unique(dataset['labels'], return_counts=True)[1]}")

        return datasets

class AdultDatasetProcessor:
    def __init__(self, data_path='../adult/adult.data', test_path='../adult/adult.test'):
        self.data_path = data_path
        self.test_path = test_path

        self.column_names = [
            'age', 'workclass', 'fnlwgt', 'education', 'education-num','marital-status', 'occupation', 'relationship', 'race', 'sex',
            'capital-gain', 'capital-loss', 'hours-per-week', 'native-country', 'income'
        ]

    def load_data(self):
        print("\nLoading Adult dataset")

        train_df = pd.read_csv(self.data_path, names=self.column_names,skipinitialspace=True, na_values='?', engine='python')
        test_df = pd.read_csv(self.test_path, names=self.column_names,skipinitialspace=True, na_values='?', skiprows=1, engine='python')

        print(f"Raw training data: {train_df.shape}")
        print(f"Raw test data: {test_df.shape}")

        return train_df, test_df

    def clean_data(self, df, is_test=False, train_columns=None):
        df = df.copy()
        df = df.dropna()

        df['income'] = df['income'].str.replace('.', '', regex=False).str.strip()

        df['income'] = (df['income'] == '>50K').astype(int)
        y = 2 * df['income'].values - 1  # Convert {0, 1} to {-1, 1}

        # Use 'sex' as the sensitive attribute: Male is 1, Female is 0
        z = (df['sex'] == 'Male').astype(int).values

        numerical_features = ['age', 'fnlwgt', 'education-num', 'capital-gain','capital-loss', 'hours-per-week']
        X_numerical = df[numerical_features].values
        categorical_features = ['workclass', 'marital-status', 'occupation','relationship', 'race', 'native-country']
        df_encoded = pd.get_dummies(df[categorical_features], drop_first=True)

        if train_columns is not None:
            for col in train_columns:
                if col not in df_encoded.columns:
                    df_encoded[col] = 0
            df_encoded = df_encoded[train_columns]

        X_categorical = df_encoded.values
        X = np.hstack([X_numerical, X_categorical])

        if train_columns is None:
            return X, z, y, df_encoded.columns.tolist()
        else:
            return X, z, y

    def process_and_save(self, output_dir='data'):
        os.makedirs(output_dir, exist_ok=True)

        print("Processing adult dataset:")        
        train_df, test_df = self.load_data()

        print("\nCleaning training data...")
        X_train, z_train, y_train, train_columns = self.clean_data(train_df, is_test=False)
        print(f"Training features shape: {X_train.shape}")
        print(f"Training labels (y=-1/y=1): {np.unique(y_train, return_counts=True)[1]}")
        print(f"Training sensitive attr (z=0/z=1): {np.bincount(z_train)}")

        print("\nCleaning test data...")
        X_test, z_test, y_test = self.clean_data(test_df, is_test=True, train_columns=train_columns)
        print(f"Test features shape: {X_test.shape}")
        print(f"Test labels (y=-1/y=1): {np.unique(y_test, return_counts=True)[1]}")
        print(f"Test sensitive attr (z=0/z=1): {np.bincount(z_test)}")

        print("\nNormalizing features...")
        mean = X_train.mean(axis=0)
        std = X_train.std(axis=0)
        std[std == 0] = 1  

        X_train_normalized = (X_train - mean) / std
        X_test_normalized = (X_test - mean) / std
        train_save_path = os.path.join(output_dir, 'adult_train.npz')
        test_save_path = os.path.join(output_dir, 'adult_test.npz')
        np.savez(train_save_path,features=X_train_normalized,sensitive_attr=z_train,labels=y_train)
        np.savez(test_save_path,features=X_test_normalized,sensitive_attr=z_test,labels=y_test)

        print(f"\nSaved training data to: {train_save_path}")
        print(f"Saved test data to: {test_save_path}")
        
        return {
            'train': {'features': X_train_normalized, 'sensitive_attr': z_train, 'labels': y_train},
            'test': {'features': X_test_normalized, 'sensitive_attr': z_test, 'labels': y_test}
        }

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


def main():
    print("Assignment-1")
    print("Task A: Data Generation")
    print("Roll Number: 22CS30056")
    
    output_dir = 'data'
    os.makedirs(output_dir, exist_ok=True)

    print("Part 1: generating synthetic datasets")
    generator = SyntheticDataGenerator(n_samples=10000, random_seed=42)
    synthetic_datasets = generator.generate_all_synthetic_datasets(output_dir)
    print("Splitting syntehtic datasets (80% train, 20% test)")
    
    angle_names = ['pi', 'pi_2', 'pi_4', 'pi_6', 'pi_8']
    angle_labels = ['π', 'π/2', 'π/4', 'π/6', 'π/8']

    for i, dataset in enumerate(synthetic_datasets):
        train_data, test_data = split_dataset(dataset, test_size=0.2, random_seed=42)
        train_path = os.path.join(output_dir, f'synthetic_{angle_names[i]}_train.npz')
        test_path = os.path.join(output_dir, f'synthetic_{angle_names[i]}_test.npz')

        np.savez(train_path, **train_data)
        np.savez(test_path, **test_data)

        print(f"\n[{i+1}/5] {angle_labels[i]}: Train={len(train_data['labels'])}, Test={len(test_data['labels'])}")
        print(f"Saved: {train_path}")
        print(f"Saved: {test_path}")

    print("PART 2: processign sdult dataset")
    
    processor = AdultDatasetProcessor()
    adult_data = processor.process_and_save(output_dir)

    print("All completed till here")
    
    print(f"\nAll datasets saved in '{output_dir}/' directory:")
    print("\nSynthetic datasets (5 rotation angles):")
    for i, angle in enumerate(angle_labels):
        print(f" - synthetic_{angle_names[i]}_train.npz / synthetic_{angle_names[i]}_test.npz  [{angle}]")
    print("\nAdult dataset:")
    print(f" - adult_train.npz / adult_test.npz")
    print("\n" + "="*70)


if __name__ == "__main__":
    main()
