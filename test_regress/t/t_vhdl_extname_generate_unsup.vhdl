-- DESCRIPTION: Verilator: VHDL external name through an indexed generate
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An external name that steps through an indexed generate statement, gen(0), is not
-- supported yet.

entity t is
  port (o : out bit);
end entity t;

architecture rtl of t is
begin
  o <= << signal u.gen(0).x : bit >>;
end architecture rtl;
