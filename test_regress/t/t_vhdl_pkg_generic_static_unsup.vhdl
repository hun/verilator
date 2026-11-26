-- DESCRIPTION: Verilator: VHDL package generic integer actual that is not static is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

package gp is
  generic (n : integer);
end package gp;
package inst is new work.gp generic map (n => q);
entity t is port (o : out bit); end entity t;
architecture rtl of t is begin o <= '1'; end architecture rtl;
