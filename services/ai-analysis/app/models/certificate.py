"""SQLAlchemy model for existing certificate table (READ-ONLY)."""

from sqlalchemy import Boolean, Column, DateTime, Integer, String, Text
from sqlalchemy.dialects.postgresql import ARRAY, JSONB

from app.database import Base


class Certificate(Base):
    """Maps to the existing 'certificate' table. Read-only access."""

    __tablename__ = "certificate"

    id = Column(String(128), primary_key=True)
    certificate_type = Column(String(20))
    country_code = Column(String(10))
    subject_dn = Column(Text)
    issuer_dn = Column(Text)
    serial_number = Column(Text)
    fingerprint_sha256 = Column(String(128))
    not_before = Column(DateTime(timezone=True))
    not_after = Column(DateTime(timezone=True))

    # X.509 metadata
    version = Column(Integer)
    signature_algorithm = Column(String(100))
    signature_hash_algorithm = Column(String(50))
    public_key_algorithm = Column(String(50))
    public_key_size = Column(Integer)
    public_key_curve = Column(String(50))
    key_usage = Column(Text)  # Stored as comma-separated or ARRAY
    extended_key_usage = Column(Text)
    is_ca = Column(Boolean)
    path_len_constraint = Column(Integer)
    subject_key_identifier = Column(Text)
    authority_key_identifier = Column(Text)
    crl_distribution_points = Column(Text)
    ocsp_responder_url = Column(Text)
    is_self_signed = Column(Boolean)

    # Status
    validation_status = Column(String(30))
    validation_message = Column(Text)
    source_type = Column(String(30))
    stored_in_ldap = Column(Boolean)
