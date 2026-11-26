-- DESCRIPTION: Verilator: VHDL function array local assigned whole under a run-time condition
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
    sel : in  std_logic_vector(1 downto 0);
    o   : out std_logic_vector(3 downto 0)
  );
end entity t;

architecture rtl of t is
  type arr_t is array (0 to 3) of std_logic_vector(1 downto 0);
  function f (a : std_logic_vector(1 downto 0)) return std_logic_vector is
    variable v : arr_t := (others => "00");
  begin
    if a(0) = '1' then
      v := (others => "11");
    else
      v := (others => "01");
    end if;
    return v(0) & v(3);
  end function f;
begin
  o <= f(sel);
end architecture rtl;
