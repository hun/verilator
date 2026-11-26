-- DESCRIPTION: Verilator: VHDL port name list without a colon
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
--
-- A name list in an interface declaration must be followed by a colon and the subtype. The colon is missing after the second name.

entity t is
  port (a, b bit);
end entity t;

architecture rtl of t is
begin
end architecture rtl;
