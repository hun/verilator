-- DESCRIPTION: Verilator: a folded function that writes too many elements
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A folded function binds its locals to values, and every element write rebuilds the
-- whole value. This body is legal VHDL but its fold would copy more than the frontend
-- allows, so it has to be reported instead of folded.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_fold_budget_unsup is
  port (
    o : out std_logic
  );
end entity;

architecture rtl of t_vhdl_fold_budget_unsup is
  function fill (n : natural) return std_logic is
    variable bits : std_logic_vector(1048575 downto 0);
  begin
    for i in 0 to 19 loop
      bits(i) := '1';
    end loop;
    return bits(n);
  end function;
begin
  o <= fill(3);
end architecture;
