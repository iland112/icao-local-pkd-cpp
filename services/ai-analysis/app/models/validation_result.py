"""SQLAlchemy model for existing validation_result table (READ-ONLY)."""

from sqlalchemy import Boolean, Column, DateTime, Float, Integer, String, Text

from app.database import Base


class ValidationResult(Base):
    """Maps to the existing 'validation_result' table. Read-only access."""

    __tablename__ = "validation_result"

    id = Column(String(128), primary_key=True)
    certificate_id = Column(String(128))  # fingerprint_sha256
    upload_id = Column(String(128))
    certificate_type = Column(String(20))
    country_code = Column(String(10))
    subject_dn = Column(Text)
    issuer_dn = Column(Text)
    serial_number = Column(Text)
    validation_status = Column(String(30))
    validation_timestamp = Column(DateTime(timezone=True))

    # Trust Chain
    trust_chain_valid = Column(Boolean)
    trust_chain_message = Column(Text)
    trust_chain_path = Column(Text)

    # CSCA Lookup
    csca_found = Column(Boolean)
    csca_subject_dn = Column(Text)

    # Signature
    signature_valid = Column(Boolean)
    signature_algorithm = Column(String(100))

    # ICAO 9303 Compliance
    icao_compliant = Column(Boolean)
    icao_compliance_level = Column(String(30))
    icao_violations = Column(Text)
    icao_key_usage_compliant = Column(Boolean)
    icao_algorithm_compliant = Column(Boolean)
    icao_key_size_compliant = Column(Boolean)
    icao_validity_period_compliant = Column(Boolean)
    icao_extensions_compliant = Column(Boolean)

    # Revocation
    revocation_status = Column(String(30))
