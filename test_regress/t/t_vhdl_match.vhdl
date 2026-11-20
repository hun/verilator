-- DESCRIPTION: Verilator: VHDL match operators
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a    : in std_logic_vector(3 downto 0);
    b    : in std_logic_vector(3 downto 0);
    z_eq : out std_logic;
    z_ne : out std_logic;
    z_mid : out std_logic;
    z_ab : out std_logic
  );
end;

architecture rtl of t is
begin
  -- '-' in a literal is a don't-care position
  z_eq <= a ?= "1--0";
  z_ne <= a ?/= "1--0";
  z_mid <= a ?= "10-1";
  -- Without a wildcard, matching is same-value equality
  z_ab <= a ?= b;
end;
