-- DESCRIPTION: Verilator: VHDL too many index constraints for an array subtype
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A bit vector's element is a scalar, so a third index constraint has no level.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  type v is array (natural range <>) of bit;
  signal s : v(0 to 3)(0 to 1)(0 to 2);
begin
  o <= '0';
end architecture rtl;
