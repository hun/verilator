-- DESCRIPTION: Verilator: VHDL type declared from the subtype of an object
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
    src : in  std_ulogic_vector(15 downto 0);
    a_o : out std_ulogic_vector(15 downto 0);
    w_o : out std_ulogic_vector(15 downto 0)
  );
end;

architecture rtl of t is
  -- The port's subtype is known only after the ports are declared, so both
  -- types are resolved at their first use
  type array_t is array (0 to 1) of src'subtype;
  subtype word_t is src'subtype;
  signal arr : array_t;
  signal w : word_t;
begin
  arr(0) <= src;
  arr(1) <= not src;
  w <= arr(1);
  a_o <= arr(0);
  w_o <= w;
end;
