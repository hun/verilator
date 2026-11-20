-- DESCRIPTION: Verilator: VHDL a concurrent assertion in an entity statement part
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An entity may state assertions of its own, beside its architectures (IEEE
-- 1076-2008 8.3). The assertion here fails while ``d`` is low and ``STRICT`` is
-- false; `severity error` reports and the model keeps running, which is what the
-- harness checks. Every value below is the one GHDL reports.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_entity_assert is
  generic (
    STRICT : boolean := false
  );
  port (
    d : in  std_logic;
    q : out std_logic
  );
begin
  assert STRICT or (d = '1') report "d must be one" severity error;
end entity;

library ieee;
use ieee.std_logic_1164.all;

architecture rtl of t_vhdl_entity_assert is
begin
  q <= d;
end architecture;