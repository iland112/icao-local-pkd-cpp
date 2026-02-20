"""Anomaly detection using Isolation Forest + Local Outlier Factor."""

import logging

import numpy as np
from sklearn.ensemble import IsolationForest
from sklearn.neighbors import LocalOutlierFactor
from sklearn.preprocessing import StandardScaler

from app.config import get_settings
from app.services.feature_engineering import FEATURE_EXPLANATIONS_KO, FEATURE_NAMES

logger = logging.getLogger(__name__)


class AnomalyDetector:
    """Dual-model anomaly detection: Isolation Forest (global) + LOF (local)."""

    def __init__(self):
        settings = get_settings()
        self.scaler = StandardScaler()
        self.isolation_forest = IsolationForest(
            contamination=settings.anomaly_contamination,
            n_estimators=200,
            random_state=42,
            n_jobs=-1,
        )
        self.lof = LocalOutlierFactor(
            n_neighbors=settings.lof_neighbors,
            contamination=settings.anomaly_contamination,
            novelty=False,
            n_jobs=-1,
        )
        self._fitted = False

    def fit_predict(
        self, features: np.ndarray
    ) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[list[str]]]:
        """Fit models and predict anomaly scores for all certificates.

        Returns:
            (combined_scores, if_scores, lof_scores, explanations)
            - combined_scores: 0.0 (normal) ~ 1.0 (anomalous)
            - if_scores: Isolation Forest normalized scores
            - lof_scores: LOF normalized scores
            - explanations: list of top-5 contributing feature explanations per cert
        """
        n = features.shape[0]
        logger.info("Fitting anomaly detection models on %d samples...", n)

        # Scale features
        scaled = self.scaler.fit_transform(features)

        # Isolation Forest
        self.isolation_forest.fit(scaled)
        if_raw = self.isolation_forest.decision_function(scaled)
        # decision_function: higher = more normal. Invert and normalize to 0~1
        if_scores = self._normalize_scores(-if_raw)

        # Local Outlier Factor
        lof_labels = self.lof.fit_predict(scaled)
        lof_raw = self.lof.negative_outlier_factor_
        # negative_outlier_factor_: closer to -1 = normal, more negative = outlier
        lof_scores = self._normalize_scores(-lof_raw - 1.0)

        # Combined score: 60% IF + 40% LOF
        combined = 0.6 * if_scores + 0.4 * lof_scores

        # Generate explanations for each certificate
        explanations = self._generate_explanations(scaled, combined)

        self._fitted = True
        logger.info(
            "Anomaly detection complete: %.1f%% anomalous, %.1f%% suspicious",
            100.0 * np.mean(combined >= 0.7),
            100.0 * np.mean((combined >= 0.3) & (combined < 0.7)),
        )

        return combined, if_scores, lof_scores, explanations

    def _normalize_scores(self, scores: np.ndarray) -> np.ndarray:
        """Normalize scores to 0.0 ~ 1.0 range."""
        smin, smax = scores.min(), scores.max()
        if smax - smin < 1e-10:
            return np.zeros_like(scores)
        return (scores - smin) / (smax - smin)

    def _generate_explanations(
        self, scaled_features: np.ndarray, anomaly_scores: np.ndarray
    ) -> list[list[str]]:
        """Generate human-readable explanations for anomalous certificates.

        For each certificate, find the top-5 features that deviate most from the mean
        (weighted by how anomalous the certificate is).
        """
        explanations = []
        mean = scaled_features.mean(axis=0)
        std = scaled_features.std(axis=0)
        std[std < 1e-10] = 1.0  # avoid division by zero

        for i in range(len(anomaly_scores)):
            if anomaly_scores[i] < 0.3:
                # Normal certificates get no explanation
                explanations.append([])
                continue

            # Z-score per feature: how far from mean
            deviations = np.abs(scaled_features[i] - mean) / std

            # Top 5 deviating features
            top_indices = np.argsort(deviations)[-5:][::-1]
            cert_explanations = []
            for idx in top_indices:
                if deviations[idx] > 1.0:  # only report significant deviations
                    feat_name = FEATURE_NAMES[idx]
                    ko_name = FEATURE_EXPLANATIONS_KO.get(feat_name, feat_name)
                    direction = "높음" if scaled_features[i, idx] > mean[idx] else "낮음"
                    cert_explanations.append(
                        f"{ko_name}: 평균 대비 {deviations[idx]:.1f}σ {direction}"
                    )
            explanations.append(cert_explanations)

        return explanations


def classify_anomaly(score: float) -> str:
    """Classify anomaly score into label."""
    if score >= 0.7:
        return "ANOMALOUS"
    elif score >= 0.3:
        return "SUSPICIOUS"
    return "NORMAL"
