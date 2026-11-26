-- DESCRIPTION: Verilator: a selected-assignment choice that is not static is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A choice must be a locally static expression (IEEE 1076-2008 10.9).
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    s, lo : in bit_vector(0 to 1);
    y : out bit
  );
end entity t;

architecture rtl of t is

begin
  with s select y <= '1' when lo, '0' when others;
end architecture rtl;
