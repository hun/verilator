-- DESCRIPTION: Verilator: VHDL bit-string literal compared with an array of enumerations
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A bit-string literal is an array of bit or std_logic, so it has no array type of
-- enumeration elements to compare with (IEEE 1076-2008 9.3.1, 9.2.3). GHDL rejects the
-- comparison below.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  type st is (aa, bb, cc);
  type ea is array (natural range <>) of st;
  signal s : ea(0 to 3);
begin
  o <= '1' when s < "0101" else '0';
end architecture rtl;
