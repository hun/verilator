-- DESCRIPTION: Verilator: an enumeration literal shared by two types without a context is ambiguous
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package enum_ambig_pkg is
  type a_t is (a0, shared);
  type b_t is (shared, b1);
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.enum_ambig_pkg.all;

entity t is
  port (o : out bit);
end entity;

architecture rtl of t is
begin
  -- No expected type selects one of the two visible 'shared' literals
  o <= '1' when shared = shared else '0';
end architecture;
