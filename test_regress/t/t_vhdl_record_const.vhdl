-- DESCRIPTION: Verilator: a record constant whose members are unconstrained
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A record type with unconstrained members takes each member's bounds from the
-- constant's aggregate, as a vector takes its bounds from its initial value. This is how
-- open-logic's CRC catalogue declares its settings records; before the fix the package
-- was refused with "aggregate that does not cover every element".

library ieee;
use ieee.std_logic_1164.all;

package pk is
  type cfg_t is record
    poly    : std_logic_vector;
    initial : std_logic_vector;
    name    : string;
    flip    : boolean;
  end record;

  constant Cfg_c : cfg_t := (poly => x"2F", initial => "1010", name => "MSB_FIRST",
                             flip => true);
  constant Cfg2_c : cfg_t := (poly => x"A7", initial => x"00", name => "LSB_FIRST",
                              flip => false);
  constant Tiny_c : cfg_t := (poly => "0", initial => "0", name => "X", flip => false);
end package;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    o : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of t is
  constant Value_c : std_logic_vector(7 downto 0) := "10100101";
begin
  o <= Value_c;
end architecture;
