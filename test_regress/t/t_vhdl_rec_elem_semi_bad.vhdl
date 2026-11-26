-- DESCRIPTION: Verilator: VHDL missing semicolon after a record element
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The element a has no terminating semicolon before element b.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
  type r is record
    a : bit
    b : bit;
  end record;
begin
end architecture rtl;
