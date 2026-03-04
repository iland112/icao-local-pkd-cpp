"""Tests for app.database -- safe_json_loads and safe_isna utilities."""

import json

import numpy as np
import pandas as pd
import pytest


class TestSafeJsonLoads:
    """Test safe_json_loads() for PostgreSQL JSONB and Oracle CLOB handling."""

    def test_none_returns_default_dict(self):
        from app.database import safe_json_loads
        result = safe_json_loads(None)
        assert result == {}

    def test_none_with_custom_default(self):
        from app.database import safe_json_loads
        result = safe_json_loads(None, default=[])
        assert result == []

    def test_dict_input_returned_as_is(self):
        """PostgreSQL JSONB returns Python dict -- should pass through."""
        from app.database import safe_json_loads
        data = {"key": "value", "count": 42}
        result = safe_json_loads(data)
        assert result is data  # same object, not copied

    def test_list_input_returned_as_is(self):
        """PostgreSQL JSONB list should pass through."""
        from app.database import safe_json_loads
        data = [1, 2, 3]
        result = safe_json_loads(data)
        assert result is data

    def test_valid_json_string_parsed(self):
        """Oracle CLOB returns JSON string -- should be parsed."""
        from app.database import safe_json_loads
        json_str = '{"algorithm": 5.0, "key_size": 0.0}'
        result = safe_json_loads(json_str)
        assert result == {"algorithm": 5.0, "key_size": 0.0}

    def test_json_array_string_parsed(self):
        from app.database import safe_json_loads
        json_str = '[{"feature": "key_size", "sigma": 3.2}]'
        result = safe_json_loads(json_str)
        assert isinstance(result, list)
        assert len(result) == 1

    def test_invalid_json_returns_default(self):
        from app.database import safe_json_loads
        result = safe_json_loads("not valid json {{{")
        assert result == {}

    def test_invalid_json_with_custom_default(self):
        from app.database import safe_json_loads
        result = safe_json_loads("not valid json", default=[])
        assert result == []

    def test_empty_string_returns_default(self):
        from app.database import safe_json_loads
        result = safe_json_loads("")
        assert result == {}

    def test_whitespace_string_returns_default(self):
        from app.database import safe_json_loads
        result = safe_json_loads("   ")
        assert result == {}

    def test_integer_input_returned_as_is(self):
        """Non-string, non-None values should pass through."""
        from app.database import safe_json_loads
        result = safe_json_loads(42)
        assert result == 42

    def test_nested_json_string(self):
        from app.database import safe_json_loads
        data = {
            "score": 8.5,
            "categories": {"algorithm": 5.0, "key_size": 0.0},
            "findings": [],
        }
        result = safe_json_loads(json.dumps(data))
        assert result == data


class TestSafeIsna:
    """Test safe_isna() for handling various NA-like values safely."""

    def test_none_is_na(self):
        from app.database import safe_isna
        assert safe_isna(None) is True

    def test_nan_is_na(self):
        from app.database import safe_isna
        assert safe_isna(float("nan")) is True

    def test_numpy_nan_is_na(self):
        from app.database import safe_isna
        assert safe_isna(np.nan) is True

    def test_pd_nat_is_na(self):
        from app.database import safe_isna
        assert safe_isna(pd.NaT) is True

    def test_valid_string_is_not_na(self):
        from app.database import safe_isna
        assert safe_isna("hello") is False

    def test_valid_int_is_not_na(self):
        from app.database import safe_isna
        assert safe_isna(42) is False

    def test_valid_float_is_not_na(self):
        from app.database import safe_isna
        assert safe_isna(3.14) is False

    def test_zero_is_not_na(self):
        from app.database import safe_isna
        assert safe_isna(0) is False

    def test_empty_string_is_not_na(self):
        from app.database import safe_isna
        assert safe_isna("") is False

    def test_array_input_does_not_raise(self):
        """pd.isna() raises ValueError on arrays -- safe_isna should handle it."""
        from app.database import safe_isna
        # Should not raise, should return False for non-scalar
        result = safe_isna([1, 2, 3])
        assert result is False

    def test_numpy_array_does_not_raise(self):
        from app.database import safe_isna
        result = safe_isna(np.array([1.0, np.nan, 3.0]))
        assert result is False

    def test_dict_input_does_not_raise(self):
        from app.database import safe_isna
        result = safe_isna({"key": "value"})
        assert result is False
