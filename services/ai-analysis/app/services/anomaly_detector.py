"""Anomaly detection using Isolation Forest + Local Outlier Factor.

v2.19.0: Type-specific models — separate IF+LOF per certificate type.
"""

import logging

import numpy as np
from sklearn.ensemble import IsolationForest
from sklearn.neighbors import LocalOutlierFactor
from sklearn.preprocessing import StandardScaler

from app.config import get_settings
from app.services.feature_engineering import FEATURE_EXPLANATIONS_KO, FEATURE_NAMES

logger = logging.getLogger(__name__)

# Type-specific model configuration
TYPE_CONFIG = {
    "CSCA": {
        "contamination": 0.05,
        "lof_neighbors": 15,
        "min_samples": 30,  # minimum samples for ML model
    },
    "DSC": {
        "contamination": 0.05,
        "lof_neighbors": 20,
        "min_samples": 30,
    },
    "DSC_NC": {
        "contamination": 0.10,  # higher expected anomaly rate
        "lof_neighbors": 15,
        "min_samples": 30,
    },
    "MLSC": {
        "contamination": 0.05,
        "lof_neighbors": 5,
        "min_samples": 10,  # small dataset, use rule-based fallback
    },
}


class AnomalyDetector:
    """Type-specific dual-model anomaly detection.

    For each certificate type with sufficient data: Isolation Forest + LOF.
    For small datasets (< min_samples): rule-based fallback scoring.
    """

    def __init__(self):
        settings = get_settings()
        self._default_contamination = settings.anomaly_contamination
        self._default_lof_neighbors = settings.lof_neighbors
        self._fitted = False

    def fit_predict(
        self, features: np.ndarray, cert_types: np.ndarray | None = None,
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[list[str]]]:
        """Fit models and predict anomaly scores for all certificates.

        Args:
            features: Feature matrix (n x 45)
            cert_types: Optional array of certificate types per row.
                If None, uses single-model mode (backward compatible).

        Returns:
            (combined_scores, if_scores, lof_scores, explanations)
        """
        n = features.shape[0]

        if cert_types is not None and len(cert_types) == n:
            return self._fit_predict_by_type(features, cert_types)

        # Fallback: single model mode (backward compatible)
        return self._fit_predict_single(features)

    def _fit_predict_single(
        self, features: np.ndarray,
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[list[str]]]:
        """Original single-model approach (backward compatible)."""
        settings = get_settings()
        n = features.shape[0]
        logger.info("Fitting single anomaly detection model on %d samples...", n)

        scaler = StandardScaler()
        scaled = scaler.fit_transform(features)

        # Isolation Forest
        iso = IsolationForest(
            contamination=settings.anomaly_contamination,
            n_estimators=200,
            random_state=42,
            n_jobs=-1,
        )
        iso.fit(scaled)
        if_raw = iso.decision_function(scaled)
        if_scores = self._normalize_scores(-if_raw)

        # LOF
        lof = LocalOutlierFactor(
            n_neighbors=min(settings.lof_neighbors, max(n - 1, 2)),
            contamination=settings.anomaly_contamination,
            novelty=False,
            n_jobs=-1,
        )
        lof.fit_predict(scaled)
        lof_raw = lof.negative_outlier_factor_
        lof_scores = self._normalize_scores(-lof_raw - 1.0)

        combined = 0.6 * if_scores + 0.4 * lof_scores
        explanations = self._generate_explanations(scaled, combined)

        self._fitted = True
        self._log_results(combined)
        return combined, if_scores, lof_scores, explanations

    def _fit_predict_by_type(
        self, features: np.ndarray, cert_types: np.ndarray,
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[list[str]]]:
        """Type-specific model approach."""
        n = features.shape[0]
        combined = np.zeros(n, dtype=np.float64)
        if_scores = np.zeros(n, dtype=np.float64)
        lof_scores = np.zeros(n, dtype=np.float64)
        explanations: list[list[str]] = [[] for _ in range(n)]

        unique_types = np.unique(cert_types)
        logger.info(
            "Fitting type-specific models for %d types on %d samples...",
            len(unique_types), n,
        )

        for ct in unique_types:
            mask = cert_types == ct
            indices = np.where(mask)[0]
            type_features = features[indices]
            type_n = len(indices)

            config = TYPE_CONFIG.get(ct, TYPE_CONFIG["DSC"])
            min_samples = config["min_samples"]

            if type_n < min_samples:
                # Rule-based fallback for small datasets
                logger.info(
                    "Type %s: %d samples < %d, using rule-based fallback",
                    ct, type_n, min_samples,
                )
                type_combined = self._rule_based_scoring(type_features)
                combined[indices] = type_combined
                if_scores[indices] = type_combined
                lof_scores[indices] = type_combined
                # Generate explanations for rule-based scores
                scaler = StandardScaler()
                scaled = scaler.fit_transform(type_features)
                type_explanations = self._generate_explanations(scaled, type_combined)
                for j, idx in enumerate(indices):
                    explanations[idx] = type_explanations[j]
                continue

            logger.info("Type %s: fitting IF+LOF on %d samples...", ct, type_n)

            scaler = StandardScaler()
            scaled = scaler.fit_transform(type_features)

            # Isolation Forest
            iso = IsolationForest(
                contamination=config["contamination"],
                n_estimators=200,
                random_state=42,
                n_jobs=-1,
            )
            iso.fit(scaled)
            if_raw = iso.decision_function(scaled)
            type_if = self._normalize_scores(-if_raw)

            # LOF
            lof_neighbors = min(config["lof_neighbors"], max(type_n - 1, 2))
            lof = LocalOutlierFactor(
                n_neighbors=lof_neighbors,
                contamination=config["contamination"],
                novelty=False,
                n_jobs=-1,
            )
            lof.fit_predict(scaled)
            lof_raw = lof.negative_outlier_factor_
            type_lof = self._normalize_scores(-lof_raw - 1.0)

            type_combined = 0.6 * type_if + 0.4 * type_lof

            combined[indices] = type_combined
            if_scores[indices] = type_if
            lof_scores[indices] = type_lof

            type_explanations = self._generate_explanations(scaled, type_combined)
            for j, idx in enumerate(indices):
                explanations[idx] = type_explanations[j]

        self._fitted = True
        self._log_results(combined)
        return combined, if_scores, lof_scores, explanations

    def _rule_based_scoring(self, features: np.ndarray) -> np.ndarray:
        """Simple rule-based anomaly scoring for small datasets.

        Uses feature value deviation from median as anomaly indicator.
        """
        n = features.shape[0]
        if n == 0:
            return np.zeros(0)

        median = np.median(features, axis=0)
        mad = np.median(np.abs(features - median), axis=0)
        mad[mad < 1e-10] = 1.0  # avoid division by zero

        scores = np.zeros(n)
        for i in range(n):
            deviations = np.abs(features[i] - median) / mad
            # Average of top-10 deviations, normalized
            top_devs = np.sort(deviations)[-10:]
            scores[i] = min(np.mean(top_devs) / 5.0, 1.0)

        return scores

    def _normalize_scores(self, scores: np.ndarray) -> np.ndarray:
        """Normalize scores to 0.0 ~ 1.0 range."""
        smin, smax = scores.min(), scores.max()
        if smax - smin < 1e-10:
            return np.zeros_like(scores)
        return (scores - smin) / (smax - smin)

    def _generate_explanations(
        self, scaled_features: np.ndarray, anomaly_scores: np.ndarray
    ) -> list[list[str]]:
        """Generate human-readable explanations for anomalous certificates."""
        explanations = []
        n_features = scaled_features.shape[1]
        mean = scaled_features.mean(axis=0)
        std = scaled_features.std(axis=0)
        std[std < 1e-10] = 1.0

        for i in range(len(anomaly_scores)):
            if anomaly_scores[i] < 0.3:
                explanations.append([])
                continue

            deviations = np.abs(scaled_features[i] - mean) / std
            top_indices = np.argsort(deviations)[-5:][::-1]
            cert_explanations = []
            for idx in top_indices:
                if deviations[idx] > 1.0 and idx < len(FEATURE_NAMES):
                    feat_name = FEATURE_NAMES[idx]
                    ko_name = FEATURE_EXPLANATIONS_KO.get(feat_name, feat_name)
                    direction = "높음" if scaled_features[i, idx] > mean[idx] else "낮음"
                    cert_explanations.append(
                        f"{ko_name}: 평균 대비 {deviations[idx]:.1f}σ {direction}"
                    )
            explanations.append(cert_explanations)

        return explanations

    def _log_results(self, combined: np.ndarray):
        """Log summary statistics."""
        logger.info(
            "Anomaly detection complete: %.1f%% anomalous, %.1f%% suspicious",
            100.0 * np.mean(combined >= 0.7),
            100.0 * np.mean((combined >= 0.3) & (combined < 0.7)),
        )


def classify_anomaly(score: float) -> str:
    """Classify anomaly score into label."""
    if score >= 0.7:
        return "ANOMALOUS"
    elif score >= 0.3:
        return "SUSPICIOUS"
    return "NORMAL"
