-- TODO: c_since ON UPDATE CURRENT_TIMESTAMP,
DROP TABLE if exists dim;
CREATE TABLE dim (
  d_id int NOT NULL,
  d_value int NOT NULL,
  PRIMARY KEY (d_id)
);
CREATE INDEX IDX_DIM_ID ON dim (d_id);

DROP TABLE if exists fact;
CREATE TABLE fact (
  f_id int NOT NULL,
  f_d_id int NOT NULL,
  PRIMARY KEY (f_id)
);

CREATE INDEX IDX_FACT_ID ON fact (f_d_id);
