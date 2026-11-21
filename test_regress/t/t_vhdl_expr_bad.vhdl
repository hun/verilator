-- DESCRIPTION: Verilator: VHDL expression and name diagnostic
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Assignments to constants and inputs, invalid names, slices, aliases and aggregates.

package p is
  constant k : integer := 1;
end package;

entity top is
  port (i : in bit; o : out bit; cmp : out boolean; q : out bit_vector(3 downto 0));
end entity;

architecture rtl of top is
  type r is record
    a : bit;
  end record;
  type v1 is array (0 to 1) of bit_vector(3 downto 0);
  type rv is record
    a : bit_vector(1 downto 0);
  end record;
  subtype rvq is rv(b(1 downto 0));
  constant c : integer := 1;
  constant rc : r := (b => '0');
  constant v : bit_vector(3 downto 0) := "0000";
  constant e : bit_vector(1 downto 0) := v(2 to 1);
  constant n1 : bit_vector(1 downto 0) := v(nosuch'range);
  constant k1 : integer := p.nosuch;
  alias a : bit is a;
  signal xs : v1;
begin
  o <= a;
  q <= v(9 downto 6);
  cmp <= ("0000", "1111") < xs;
  process
  begin
    c := 2;
    i <= '1';
    wait;
  end process;
end architecture;
