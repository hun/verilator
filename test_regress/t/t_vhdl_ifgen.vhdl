-- DESCRIPTION: Verilator: VHDL if-generate and elsif-generate selection
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  generic (
    GEN : boolean := true;
    INV : boolean := false
  );
  port (
    a : in std_logic;
    b : in std_logic;
    c : in std_logic;
    y : out std_logic;
    z : out std_logic;
    w : out std_logic;
    v : out std_logic;
    u : out std_logic
  );
end;

architecture rtl of t is

  -- A pure function returning a constant boolean, as used by simulation-only
  -- generate branches
  function use_alt return boolean is
  begin
    return false;
  end function;

begin
  -- True branch
  g1 : if true generate
  begin
    y <= a;
  else generate
  begin
    y <= b;
  end generate;

  -- Elsif chain: both conditions false, so the final else is selected
  g2 : if false generate
  begin
    z <= a;
  elsif false generate
  begin
    z <= b;
  else generate
  begin
    z <= c;
  end generate;

  -- Boolean generics, including a negated condition
  g3 : if GEN generate
  begin
    w <= '1';
  else generate
  begin
    w <= '0';
  end generate;

  g4 : if not INV generate
  begin
    v <= a;
  else generate
  begin
    v <= b;
  end generate;

  g5 : if use_alt generate
  begin
    u <= a;
  else generate
  begin
    u <= b;
  end generate;
end;
