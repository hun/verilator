-- DESCRIPTION: Verilator: an array indexed by an enumeration value
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- poc's arith_Adder_Wide keeps a per-architecture table and indexes it with the
-- architecture generic: `DEFAULT_BLOCKING(ARCH)`. An enumeration value used as an index
-- is its position (IEEE 1076-2008 8.4), whether it is a literal, a constant or a
-- formal an inlined body bound to one.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    clk : in  std_logic;
    arch_sel : in std_logic_vector(7 downto 0);
    o   : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of t is
  type t_arch is (AAM, CAI, PAI);

  type t_vec is array (t_arch) of integer;
  constant defaults_c : t_vec := (AAM => 1, CAI => 2, PAI => 3);

  function pick(x : t_arch) return integer is
  begin
    return defaults_c(x);
  end function;

  constant chosen_c : integer := pick(PAI);
begin
  process (clk)
  begin
    if rising_edge(clk) then
      -- The table also folds through an element read of a constant array
      o <= std_logic_vector(to_unsigned(chosen_c + defaults_c(CAI), 8));
    end if;
  end process;
end architecture;
